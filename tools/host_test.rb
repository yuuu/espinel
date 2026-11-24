# Runs the host tests (test/host/*_test.rb) inside the development image:
# each is compiled by Spinel for the container's own gcc, linked with the
# stubs in test/host/stubs/ in place of the packages' ESP-IDF C, and run.
# `rake test:host` calls it; arguments name the tests to run
# (`spi` is test/host/spi_test.rb), all of them by default. The image
# carries Spinel's host runtime (lib/libspinel_rt.a) for them.
require "etc"
require "fileutils"
require "open3"

ROOT = File.expand_path("..", __dir__)
OUT = File.join(ROOT, "build-host-test")
SPINEL_DIR = ENV.fetch("SPINEL_DIR", "/opt/spinel")
SPINEL = File.join(SPINEL_DIR, "bin", "spinel")
TEST_DIR = File.join(ROOT, "test", "host")
STUBS = File.join(OUT, "libespinel_stubs.a")
RUN_SECONDS = 60

def run!(*cmd, chdir: ROOT)
  out, status = Open3.capture2e(*cmd, chdir: chdir)
  return if status.success?
  warn out
  abort "failed: #{cmd.join(" ")}"
end

# Every stub in one archive: the linker takes only the members a test uses.
def stubs
  dir = File.join(OUT, "stubs")
  FileUtils.mkdir_p(dir)
  objs = Dir[File.join(TEST_DIR, "stubs", "*.c")].sort.map do |src|
    obj = File.join(dir, "#{File.basename(src, ".c")}.o")
    run!("cc", "-c", "-O1", "-Wall", "-Werror", src, "-o", obj)
    obj
  end
  FileUtils.rm_f(STUBS)
  run!("ar", "rcs", STUBS, *objs)
end

def tests(names)
  return Dir[File.join(TEST_DIR, "*_test.rb")].sort if names.empty?

  names.map do |name|
    path = File.exist?(name) ? File.expand_path(name) : File.join(TEST_DIR, "#{name.delete_suffix("_test")}_test.rb")
    File.exist?(path) or abort "no test #{name} (#{path})"
    path
  end
end

def clock = Process.clock_gettime(Process::CLOCK_MONOTONIC)

# Compiles and runs one test: [passed, report]
def run_test(path)
  name = File.basename(path, ".rb")
  exe = File.join(OUT, "bin", name)
  started = clock
  out, status = Open3.capture2e(SPINEL, "-I", "packages", path, "-o", exe, "--link", STUBS, chdir: ROOT)
  return [false, "FAIL #{name} (compile)\n#{out}"] unless status.success?

  compiled = clock
  out, status = Open3.capture2e("timeout", RUN_SECONDS.to_s, exe, chdir: ROOT)
  timing = format("compile %.1fs, run %.1fs", compiled - started, clock - compiled)
  return [true, "PASS #{name} (#{timing}): #{out.lines.last&.strip}"] if status.success?

  reason = status.exitstatus == 124 ? "timed out after #{RUN_SECONDS}s" : "exit #{status.exitstatus || status.termsig}"
  [false, "FAIL #{name} (#{reason}; #{timing})\n#{out}"]
end

stubs
FileUtils.mkdir_p(File.join(OUT, "bin"))
# The tests compile at once, as many as there are CPUs; reports come in order.
queue = Queue.new
paths = tests(ARGV)
paths.each_with_index { |path, i| queue << [path, i] }
results = Array.new(paths.size)
Array.new([Etc.nprocessors, paths.size].min) do
  Thread.new do
    loop do
      path, i = queue.pop(true)
      results[i] = run_test(path)
    rescue ThreadError
      break
    end
  end
end.each(&:join)
results.each { |_, report| puts report }
failed = results.count { |passed, _| !passed }
puts "#{results.size} tests, #{failed} failed"
exit(failed.zero? ? 0 : 1)
