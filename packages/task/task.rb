# Ruby programs that run at once, each on a FreeRTOS task of its own.
#
# Every tasks/<name>.rb beside the entry point is built into the firmware
# as a program of its own, a VM: its own copy of Spinel's runtime, objects
# and collector, so the two never share an object and run on the two cores
# at once. Task.new(:name) starts one; it runs once a boot.
#
#   worker = Task.new(:sensor, core: 0)   # tasks/sensor.rb
#   worker.join
#   worker.value  # => its exit status
#
# They share no object, so they talk through Task::Queue, a named queue of
# Strings that every VM opens by its name:
#
#   q = Task::Queue.new(:readings)   # in both programs
#   q.push("21.5")                    # in one
#   q.pop                             # => "21.5", in the other
#
# The C half is task.c, linked once, which keeps the table of VMs (main.c
# starts the main program through it), and task_vm.c, each VM's.
module ESPTask
  ffi_func :espinel_task_find, [:str], :int
  ffi_func :espinel_task_start, [:int, :int, :int, :int], :int
  ffi_func :espinel_task_wait, [:int, :int], :int
  ffi_func :espinel_task_state, [:int], :int
  ffi_func :espinel_task_status, [:int], :int
  ffi_func :espinel_task_self, [], :int
  ffi_func :espinel_task_count, [], :int
  ffi_func :espinel_task_name, [:int], :str
  ffi_func :espinel_task_core, [], :int
  ffi_func :espinel_queue_open, [:str, :int], :int
  ffi_func :espinel_queue_send, [:int, :str, :size_t, :int, :int], :int
  ffi_func :espinel_queue_receive, [:int, :int], :binstr
  ffi_func :espinel_queue_got, [], :int
  ffi_func :espinel_queue_utf8, [], :int
  ffi_func :espinel_queue_size, [:int], :int
end

class Task
  # the VM states (espinel_task.h)
  IDLE = 0
  RUNNING = 1
  FINISHED = 2

  # The default stack, the main program's
  STACK_SIZE = 32 * 1024

  attr_reader :name

  # Starts tasks/<name>.rb on a task of its own: on core 0 or 1 (either
  # when nil), at a FreeRTOS priority (the main program runs at 5), with a
  # stack of stack_size bytes. A name not built in, or one started before,
  # raises ArgumentError.
  def initialize(name, core: nil, priority: 5, stack_size: STACK_SIZE)
    @name = name.to_s
    @id = ESPTask.espinel_task_find(@name)
    raise ArgumentError, "no task named #{@name} (tasks/#{@name}.rb)" if @id < 0
    rc = ESPTask.espinel_task_start(@id, core.nil? ? -1 : core, priority, stack_size)
    raise ArgumentError, "task #{@name} has run already (once a boot)" if rc == -2
    raise ArgumentError, "no core #{core}" if rc == -3
    raise NoMemoryError, "no memory for task #{@name}'s #{stack_size}-byte stack" if rc == -4
  end

  # Waits for the task's program to end, timeout seconds at most (forever
  # when nil). Answers self, or nil when the time ran out.
  def join(timeout = nil)
    ms = timeout.nil? ? -1 : (timeout * 1000).to_i
    rc = ESPTask.espinel_task_wait(@id, ms)
    raise ThreadError, "task #{@name} cannot join itself" if rc == -2
    rc == 1 ? self : nil
  end

  # The program's exit status, waiting for it to end
  def value
    join
    ESPTask.espinel_task_status(@id)
  end

  def alive?
    ESPTask.espinel_task_state(@id) == RUNNING
  end

  # "run" or "finished"
  def status
    ESPTask.espinel_task_state(@id) == RUNNING ? "run" : "finished"
  end

  def inspect
    "#<Task #{@name} #{status}>"
  end

  def to_s
    inspect
  end

  # The name of the program running here: "main" for the entry point
  def self.current
    ESPTask.espinel_task_name(ESPTask.espinel_task_self)
  end

  # The core the program runs on now, 0 or 1 (one not pinned can move)
  def self.core
    ESPTask.espinel_task_core
  end

  # The names of every program built in, "main" first
  def self.list
    names = []
    ESPTask.espinel_task_count.times { |i| names << ESPTask.espinel_task_name(i) }
    names
  end

  # A queue of Strings between the programs, found by its name: the first
  # Task::Queue.new(name) of a boot makes it, with room for capacity
  # messages, and every later one, in any program, opens the same queue.
  # A message is a copy of the String's bytes; its encoding is kept when it
  # is UTF-8, and binary otherwise. Up to 8 queues.
  class Queue
    CAPACITY = 8

    attr_reader :name

    def initialize(name, capacity = CAPACITY)
      @name = name.to_s
      @id = ESPTask.espinel_queue_open(@name, capacity)
      raise ArgumentError, "queue name too long: #{@name}" if @id == -1
      raise ArgumentError, "no room for another queue (8 at most)" if @id == -2
      raise NoMemoryError, "no memory for queue #{@name}" if @id < 0
    end

    # Puts a copy of str at the end, waiting while the queue is full:
    # timeout seconds at most (forever when nil). Answers self, or nil when
    # the time ran out.
    def push(str, timeout: nil)
      s = str.to_s
      rc = ESPTask.espinel_queue_send(@id, s, s.bytesize, s.encoding == Encoding::UTF_8 ? 1 : 0, ms(timeout))
      raise NoMemoryError, "no memory for a #{s.bytesize}-byte message" if rc < 0
      rc == 1 ? self : nil
    end

    def <<(str)
      push(str)
    end

    # Takes the first message, waiting for one: timeout seconds at most
    # (forever when nil). Answers the String, or nil when the time ran out.
    def pop(timeout: nil)
      s = ESPTask.espinel_queue_receive(@id, ms(timeout))
      return nil if ESPTask.espinel_queue_got == 0
      ESPTask.espinel_queue_utf8 == 1 ? s.force_encoding(Encoding::UTF_8) : s
    end

    # How many messages are waiting
    def size
      ESPTask.espinel_queue_size(@id)
    end

    def empty?
      size == 0
    end

    private

    def ms(timeout)
      timeout.nil? ? -1 : (timeout * 1000).to_i
    end
  end
end
