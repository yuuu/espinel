# Runs test/qemu/*.rb on the emulated chip and compares each program's
# output with its snapshot, test/qemu/<name>.rb.expected. `rake test:qemu`
# runs it in the container (it needs idf.py and Spinel); see README.md.
#
#   ruby tools/qemu_test.rb [--regen] [--timeout SECONDS] [--target SOC] [test/qemu/x.rb ...]
#
# Every test is its own firmware: the test is the entry point
# (-DESPINEL_MAIN, as `rake build PROJECT=` passes it) of a build in
# build-test/, whose sdkconfig adds test/qemu/sdkconfig.defaults to the
# project's. The firmware prints a line
# before the program starts and one with its exit status after it ends
# (CONFIG_ESPINEL_TEST_MARKERS, main/main.c); QEMU is stopped on the second.
#
# A test passes when the program exits with status 0 and its output (stdout
# and stderr: the UART carries both) equals the snapshot. ESP-IDF's log
# lines ("I (123) tag: ...") are left out of the output, since they carry
# a timestamp. A test fails on a build error, a non-zero status, a
# different output, a crash (a panic, an abort or a reboot) and a timeout.
# --regen writes the output as the snapshot instead of comparing, for a
# program that exits with 0.
require "fileutils"
require "optparse"
require "tmpdir"

PROJECT = File.expand_path("..", __dir__)
TEST_DIR = File.join(PROJECT, "test", "qemu")
BUILD_DIR = File.join(PROJECT, "build-test")
LOG_DIR = File.join(BUILD_DIR, "logs")

BEGIN_MARKER = "=== espinel test begin ==="
END_RE = /^=== espinel test end, status (-?\d+)\n/
# A panic prints a reason and a backtrace before "Rebooting...", which
# comes a moment later; a second ROM banner is a reboot that printed none.
CRASH_RE = /Guru Meditation Error|abort\(\) was called|Rebooting\.\.\.|Stack canary watchpoint|assert failed/
REBOOT_RE = /Rebooting\.\.\./
ROM_BANNER = "ESP-ROM:"
CRASH_GRACE = 3 # seconds to keep reading after a crash, for the backtrace
# ESP-IDF's log lines, colored or not
LOG_LINE_RE = /^(?:\e\[[0-9;]*m)?[EWIDV] \(\d+\) [^:]*: .*$\n?/

options = { regen: false, timeout: 60, target: "esp32s3" }
OptionParser.new do |o|
  o.banner = "usage: ruby tools/qemu_test.rb [options] [test/qemu/x.rb ...]"
  o.on("--regen", "write the outputs as the snapshots") { options[:regen] = true }
  o.on("--timeout SECONDS", Integer, "per test, from QEMU's start (#{options[:timeout]})") { |v| options[:timeout] = v }
  o.on("--target SOC", "the SoC to build for (#{options[:target]})") { |v| options[:target] = v }
end.parse!

files = ARGV.empty? ? Dir[File.join(TEST_DIR, "*.rb")].sort : ARGV.map { |f| File.expand_path(f, PROJECT) }
abort "no tests under test/qemu/" if files.empty?
files.each { |f| abort "#{f}: not found" unless File.file?(f) }
FileUtils.mkdir_p(LOG_DIR)

def idf_args(entry, target)
  ["idf.py", "-C", PROJECT, "-B", BUILD_DIR,
   "-DSDKCONFIG=#{File.join(BUILD_DIR, 'sdkconfig')}",
   "-DSDKCONFIG_DEFAULTS=#{File.join(PROJECT, 'sdkconfig.defaults')};#{File.join(TEST_DIR, 'sdkconfig.defaults')}",
   "-DIDF_TARGET=#{target}", "-DESPINEL_MAIN=#{entry}"]
end

def build(entry, target, log)
  pid = Process.spawn(*idf_args(entry, target), "build", in: File::NULL, %i[out err] => [log, "w"])
  Process.wait(pid)
  $?.success?
end

# Runs the firmware under QEMU until the end marker, a crash or the
# timeout. Answers [console text, status or nil, failure reason or nil].
def run_qemu(entry, target, timeout, log)
  reader, writer = IO.pipe
  pid = Process.spawn(*idf_args(entry, target), "qemu", in: File::NULL, %i[out err] => writer, pgroup: true)
  writer.close
  raw = +""
  text = ""
  status = nil
  reason = nil
  deadline = now + timeout
  loop do
    left = deadline - now
    if left <= 0
      reason ||= "timed out after #{timeout} s"
      break
    end
    next unless reader.wait_readable(left)
    chunk = begin
      reader.readpartial(4096)
    rescue EOFError
      reason ||= "QEMU exited"
      break
    end
    raw << chunk
    text = raw.gsub("\r\n", "\n") # a CR LF can be split between chunks
    if !reason && (m = END_RE.match(text))
      status = Integer(m[1])
      break
    end
    banners = text.scan(ROM_BANNER).size
    if !reason && (text =~ CRASH_RE || banners > 1)
      reason = "crashed"
      deadline = [deadline, now + CRASH_GRACE].min
    end
    break if reason && (text =~ REBOOT_RE || banners > 1)
  end
  [text, status, reason]
ensure
  stop(pid) if pid
  reader&.close
  File.write(log, text) if text
end

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def stop(pid)
  Process.kill("TERM", -pid)
  20.times do
    return if Process.wait(pid, Process::WNOHANG)
    sleep 0.1
  end
  Process.kill("KILL", -pid)
  Process.wait(pid)
rescue Errno::ESRCH, Errno::ECHILD
  nil
end

# The program's output: what comes between the markers, less ESP-IDF's log
# lines and the newline the end marker starts with.
def program_output(text)
  start = text.index("#{BEGIN_MARKER}\n") or return nil
  body = text[(start + BEGIN_MARKER.size + 1)..]
  if (m = END_RE.match(body))
    body = body[0...m.begin(0)].delete_suffix("\n")
  end
  body.gsub(LOG_LINE_RE, "")
end

def diff(expected_file, actual)
  Dir.mktmpdir do |dir|
    actual_file = File.join(dir, "actual")
    File.write(actual_file, actual)
    `diff -u --label expected --label actual #{expected_file} #{actual_file}`
  end
end

def show(text, indent = "    ")
  text.each_line { |l| puts indent + l.chomp }
end

results = files.map do |file|
  name = file.delete_prefix("#{PROJECT}/")
  base = File.basename(file, ".rb")
  print "#{name} ... "
  $stdout.flush
  build_log = File.join(LOG_DIR, "#{base}.build.log")
  unless build(file, options[:target], build_log)
    puts "FAIL (build; #{build_log.delete_prefix("#{PROJECT}/")})"
    show(File.readlines(build_log).last(30).join)
    next [name, false]
  end

  console_log = File.join(LOG_DIR, "#{base}.log")
  text, status, reason = run_qemu(file, options[:target], options[:timeout], console_log)
  output = program_output(text)
  expected_file = "#{file}.expected"
  failure =
    if reason then reason
    elsif output.nil? then "no output (the begin marker never came)"
    elsif status != 0 then "exit status #{status}"
    elsif options[:regen] then nil
    elsif !File.exist?(expected_file) then "no snapshot (#{File.basename(expected_file)}); run with REGEN=1"
    elsif File.read(expected_file) != output then "output differs"
    end

  if failure
    puts "FAIL (#{failure}; #{console_log.delete_prefix("#{PROJECT}/")})"
    if failure == "output differs"
      show(diff(expected_file, output))
    elsif output && !output.empty?
      show(output)
    else
      show(text.lines.last(30).join)
    end
    next [name, false]
  end

  if options[:regen]
    File.write(expected_file, output)
    puts "ok (snapshot written)"
  else
    puts "ok"
  end
  [name, true]
end

failed = results.reject { |_, ok| ok }
puts
puts "#{results.size} tests, #{failed.size} failed"
failed.each { |name, _| puts "  #{name}" }
exit(failed.empty? ? 0 : 1)
