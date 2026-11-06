# UART on ESP-IDF, after the UART class of the mruby / mruby/c peripheral
# interface guide (github.com/mruby/microcontroller-peripheral-interface-guide):
# a port set up with a baud rate, frame format and pins, written and read
# as Strings of bytes. The C half is uart.c in this package.
#
# Unit 0 is the console (main.c installed its driver): a UART.new(0) keeps
# that driver but applies the settings it is given, the 9600 baud default
# among them.
module ESPUART
  ffi_func :espinel_uart_port_count, [], :int
  ffi_func :espinel_uart_open, [:int], :int
  ffi_func :espinel_uart_config, [:int, :int, :int, :int, :int, :int], :int
  ffi_func :espinel_uart_set_pin, [:int, :int, :int, :int, :int], :int
  ffi_func :espinel_uart_write, [:int, :str, :int], :int
  ffi_func :espinel_uart_read, [:int, :ptr, :int], :int
  ffi_func :espinel_uart_available, [:int], :int
  ffi_func :espinel_uart_flush, [:int], :int
  ffi_func :espinel_uart_clear_rx, [:int], :int
  ffi_func :espinel_uart_send_break, [:int, :int], :int
  ffi_func :esp_err_to_name, [:int], :str
end

class UART
  # Parity (NONE, EVEN, ODD) and flow control (NONE, RTSCTS). These are
  # the package's own codes; uart.c turns them into ESP-IDF's.
  NONE = 0
  EVEN = 1
  ODD = 2
  RTSCTS = 3

  attr_reader :unit

  # UART.new(1, baudrate: 115200, txd_pin: 43, rxd_pin: 44). The unit is
  # given either way, positionally or as unit:; pins left out keep where
  # ESP-IDF routes them.
  def initialize(id = nil, unit: nil, baudrate: 9600, baud: nil, data_bits: 8,
                 stop_bits: 1, parity: NONE, flow_control: NONE,
                 txd_pin: nil, rxd_pin: nil, rts_pin: nil, cts_pin: nil)
    @unit = unit || id
    raise ArgumentError, "UART: no unit given" if @unit.nil?
    if @unit < 0 || @unit >= ESPUART.espinel_uart_port_count
      raise ArgumentError, "UART#{@unit}: no such unit"
    end
    check(ESPUART.espinel_uart_open(@unit), "setup")
    @rx = "".b
    configure(baud || baudrate, data_bits, stop_bits, parity, flow_control)
    set_pins(txd_pin, rxd_pin, rts_pin, cts_pin)
  end

  # Changes the settings given, as the constructor takes them; the others
  # stay.
  def setmode(baudrate: nil, baud: nil, data_bits: nil, stop_bits: nil,
              parity: nil, flow_control: nil,
              txd_pin: nil, rxd_pin: nil, rts_pin: nil, cts_pin: nil)
    configure(baud || baudrate || @baudrate, data_bits || @data_bits,
              stop_bits || @stop_bits, parity || @parity, flow_control || @flow_control)
    set_pins(txd_pin, rxd_pin, rts_pin, cts_pin)
    nil
  end

  # Waits until read_bytes bytes have come.
  def read(read_bytes)
    raise ArgumentError, "negative length #{read_bytes} given" if read_bytes < 0
    fill(read_bytes - @rx.bytesize) if @rx.bytesize < read_bytes
    take(read_bytes)
  end

  # Returns once the bytes are in the hardware FIFO (128 bytes), not
  # necessarily on the line yet; answers their count.
  def write(string)
    data = string.to_s
    return 0 if data.bytesize == 0
    sent = ESPUART.espinel_uart_write(@unit, data, data.bytesize)
    raise IOError, "UART#{@unit}: write failed" if sent < 0
    sent
  end

  # A line up to and including "\n", waiting until one has come.
  def gets
    loop do
      i = @rx.index("\n")
      return take(i + 1) unless i.nil?
      waiting = ESPUART.espinel_uart_available(@unit)
      fill(waiting > 0 ? waiting : 1)
    end
  end

  # Sends string and "\n", unless string ends with one already.
  def puts(string = "")
    s = string.to_s
    write(s.end_with?("\n") ? s : s + "\n")
    nil
  end

  def bytes_available
    drain
    @rx.bytesize
  end

  # Always 0: there is no TX buffer in software (see write), which is what
  # the guide answers for a device without one.
  def bytes_to_write = 0

  def can_read_line
    drain
    !@rx.index("\n").nil?
  end

  # Waits until everything queued has gone out on the line.
  def flush
    check(ESPUART.espinel_uart_flush(@unit), "flush")
    self
  end

  def clear_rx_buffer
    check(ESPUART.espinel_uart_clear_rx(@unit), "clear_rx_buffer")
    @rx = "".b
    nil
  end

  # Holds TXD low for time seconds once what is queued has gone out; by
  # default for 13 bit times, the shortest break LIN accepts.
  def send_break(time = nil)
    seconds = time.nil? ? 13.0 / @baudrate : time.to_f
    us = (seconds * 1_000_000).round
    us = 1 if us < 1
    check(ESPUART.espinel_uart_send_break(@unit, us), "send_break")
    nil
  end

  private

  def check(err, what)
    return if err == 0
    raise ArgumentError, "UART#{@unit}: #{what} failed (#{ESPUART.esp_err_to_name(err)})"
  end

  # The settings are kept once ESP-IDF has taken them.
  def configure(baudrate, data_bits, stop_bits, parity, flow_control)
    check(ESPUART.espinel_uart_config(@unit, baudrate, data_bits, stop_bits,
                                      parity, flow_control), "setup")
    @baudrate = baudrate
    @data_bits = data_bits
    @stop_bits = stop_bits
    @parity = parity
    @flow_control = flow_control
  end

  def set_pins(txd, rxd, rts, cts)
    return if txd.nil? && rxd.nil? && rts.nil? && cts.nil?
    check(ESPUART.espinel_uart_set_pin(@unit, txd || -1, rxd || -1, rts || -1, cts || -1),
          "setting the pins")
  end

  # Waits for count more bytes and keeps them.
  def fill(count)
    buf = IO::Buffer.new(count)
    got = ESPUART.espinel_uart_read(@unit, buf, count)
    raise IOError, "UART#{@unit}: read failed" if got < 0
    @rx = @rx + buf.get_string(0, got)
  end

  # Keeps whatever the driver holds, without waiting.
  def drain
    waiting = ESPUART.espinel_uart_available(@unit)
    fill(waiting) if waiting > 0
  end

  def take(count)
    s = @rx.byteslice(0, count)
    @rx = @rx.byteslice(count, @rx.bytesize - count)
    s
  end
end
