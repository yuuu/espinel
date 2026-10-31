# Tasks run on the host; everything that needs ESP-IDF or Spinel runs in the
# development container (docker/Dockerfile), the project mounted at /project.

IMAGE = "espinel-idf:v5.5.4"
SPINEL_CHECKOUT = ENV.fetch("SPINEL_CHECKOUT", "../spinel")

# Run a command in the container as the host user, so build/ and sdkconfig
# stay the user's.
def container(*cmd)
  tty = $stdin.tty? ? ["-it"] : []
  sh "docker", "run", "--rm", *tty,
     "--user", "#{Process.uid}:#{Process.gid}", "-e", "HOME=/tmp",
     "-v", "#{__dir__}:/project", "-w", "/project",
     IMAGE, *cmd
end

task default: :build

desc "Build the development image (ESP-IDF plus Spinel from #{SPINEL_CHECKOUT})"
task :image do
  sh "docker", "build", "--build-context", "spinel=#{SPINEL_CHECKOUT}",
     "-f", "docker/Dockerfile", "-t", IMAGE, "."
end

desc "Build the firmware"
task :build do
  container "idf.py", "build"
end

desc "Remove the build directory"
task :clean do
  rm_rf "build"
end

desc "Remove the build directory and the generated sdkconfig"
task clobber: :clean do
  rm_f %w[sdkconfig sdkconfig.old]
end

desc "Build and run the firmware on the emulated ESP32-S3 for SECONDS (default 15)"
task :qemu, [:seconds] => :build do |_, args|
  seconds = Integer(args[:seconds] || ENV.fetch("QEMU_SECONDS", "15"))
  container "bash", "-c", <<~SH
    timeout #{seconds} idf.py qemu </dev/null > >(sed -n '/ESP-ROM/,$p') 2>&1 &
    t=$!
    trap 'kill -TERM $t 2>/dev/null' INT TERM
    wait $t; rc=$?
    [ $rc -gt 128 ] && { wait $t; rc=$?; }
    wait
    case $rc in 124|143) exit 0 ;; *) exit $rc ;; esac
  SH
end

desc "Show the firmware's memory usage"
task :size do
  container "idf.py", "size"
end

desc "Open menuconfig"
task :menuconfig do
  container "idf.py", "menuconfig"
end

desc "Print the C that Spinel generates for lib/main.rb"
task :c do
  container "spinel", "lib/main.rb", "-S"
end

desc "Decode backtrace addresses: rake addr2line ADDRS='0x4200760a 0x4202c085'"
task :addr2line do
  addrs = ENV.fetch("ADDRS") { abort "set ADDRS to the backtrace's addresses" }.split
  container "xtensa-esp32s3-elf-addr2line", "-pfiaC", "-e", "build/espinel.elf", *addrs
end

desc "Open a shell in the container"
task :shell do
  container "bash"
end

desc "Run a command in the container: rake exec CMD='idf.py reconfigure'"
task :exec do
  cmd = ENV.fetch("CMD") { abort "set CMD to the command to run" }
  container "bash", "-c", cmd
end
