require "json"
require "esp_flash"

IMAGE = ENV.fetch("ESPINEL_IMAGE", "espinel-idf:v6.1")
SPINEL_CHECKOUT = ENV.fetch("SPINEL_CHECKOUT", "../spinel")
# The matz/spinel commit the image is built and tested with (CI checks it out)
SPINEL_VERSION = File.read(File.join(__dir__, ".spinel-version")).strip

CCACHE = ENV.fetch("CCACHE", "1") == "0" ? "0" : "1"

# The Ruby program's directory, whose main.rb is the entry point: PROJECT
# (relative to where rake was run), or lib/
def ruby_project
  dir = File.expand_path(ENV["PROJECT"].to_s.empty? ? File.join(__dir__, "lib") : ENV["PROJECT"], Rake.original_dir)
  File.file?(File.join(dir, "main.rb")) or abort "#{dir} has no main.rb; PROJECT names a directory with one"
  dir
end

# A project outside this checkout is mounted at its own path under
# /ruby-project, so a switch to another one changes the entry point and the
# build compiles it again (the same mount for both would leave the first's
# C in place when the second's files are older).
def outside_mount
  "/ruby-project#{ruby_project}"
end

def outside_project?
  !ruby_project.start_with?("#{__dir__}/")
end

# The entry point as the container sees it
def ruby_main
  return "#{outside_mount}/main.rb" if outside_project?

  File.join(ruby_project.delete_prefix("#{__dir__}/"), "main.rb")
end

def container(*cmd)
  tty = $stdin.tty? ? ["-it"] : []
  mounts = ["-v", "#{__dir__}:/project"]
  mounts += ["-v", "#{ruby_project}:#{outside_mount}"] if outside_project?
  sh "docker", "run", "--rm", *tty,
     "--user", "#{Process.uid}:#{Process.gid}", "-e", "HOME=/tmp",
     "-e", "IDF_CCACHE_ENABLE=#{CCACHE}", "-e", "CCACHE_DIR=/project/.ccache",
     *mounts, "-w", "/project",
     IMAGE, *cmd
end

task default: :build

desc "Build the development image (ESP-IDF plus Spinel from #{SPINEL_CHECKOUT})"
task :image do
  head = `git -C #{SPINEL_CHECKOUT} rev-parse HEAD 2>/dev/null`.strip
  warn "note: #{SPINEL_CHECKOUT} is at #{head.empty? ? "an unknown commit" : head}, not .spinel-version's #{SPINEL_VERSION}" if head != SPINEL_VERSION
  sh "docker", "build", "--build-context", "spinel=#{SPINEL_CHECKOUT}",
     "-f", "docker/Dockerfile", "-t", IMAGE, "."
end

def configured_target
  File.read("sdkconfig")[/^CONFIG_IDF_TARGET="(.*)"$/, 1] if File.exist?("sdkconfig")
end

def set_target(name)
  container "idf.py", "set-target", name
end

desc "Choose the SoC to build for: rake set-target TARGET=esp32c3 (regenerates sdkconfig, empties build/)"
task "set-target", [:target] do |_, args|
  name = args[:target] || ENV.fetch("TARGET") { abort "set TARGET, e.g. rake set-target TARGET=esp32c3" }
  set_target name
end

desc "Build the firmware (PROJECT=<dir> builds <dir>/main.rb in place of lib/main.rb; TARGET=<soc> switches the target first)"
task :build do
  wanted = ENV["TARGET"]
  set_target wanted if wanted && wanted != configured_target
  container "idf.py", "-DESPINEL_MAIN=#{ruby_main}", "build"
end

desc "Build lib/main.rb and every example in turn (TARGET=<soc> as for rake build); what CI runs"
task "build:all" do
  projects = ["lib", *Dir["examples/*/main.rb"].sort.map { |f| File.dirname(f) }]
  projects.each do |project|
    # An example that reads a config.rb builds with its config.example.rb
    example = File.join(project, "config.example.rb")
    config = File.join(project, "config.rb")
    stand_in = File.exist?(example) && !File.exist?(config)
    cp example, config if stand_in
    begin
      puts "== #{project}"
      ENV["PROJECT"] = File.join(__dir__, project)
      Rake::Task[:build].reenable
      Rake::Task[:build].invoke
    ensure
      rm_f config if stand_in
    end
  end
end

desc "Remove the build directory"
task :clean do
  rm_rf "build"
end

desc "Remove the build directory and the generated sdkconfig"
task clobber: :clean do
  rm_f %w[sdkconfig sdkconfig.old]
end

# clang-format runs in the image (its version comes with the Dockerfile),
# RuboCop on the host from the Gemfile. Spinel's runtime and generated C are
# not ours to format.
CLANG_FORMAT = "clang-format-20"

def own_c_files
  Dir["{main,components/spinel/port,packages}/**/*.{c,h}"].sort
end

desc "Format our C (clang-format) and Ruby (RuboCop's Layout and Lint corrections)"
task :format do
  container CLANG_FORMAT, "-i", *own_c_files
  sh "bundle", "exec", "rubocop", "--autocorrect"
end

desc "Check the C and Ruby format without changing anything (what CI runs)"
task :lint do
  container CLANG_FORMAT, "--dry-run", "--Werror", *own_c_files
  sh "bundle", "exec", "rubocop"
end

desc "Show ccache's statistics for the firmware builds"
task :ccache do
  container "ccache", "--show-stats"
end

desc "Build and run the firmware under QEMU for SECONDS (default 15)"
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

def build_json(name)
  JSON.parse(File.read(File.join(__dir__, "build", name)))
rescue Errno::ENOENT
  abort "build/#{name} is missing; run rake build first"
end

def serial_port
  EspFlash::SerialPort.resolve(ENV["PORT"])
end

desc "Build and write the firmware to the board over USB (PORT to choose the port, BAUD to change the rate)"
task flash: :build do
  args = build_json("flasher_args.json")
  flash_size = EspFlash.parse_size(args.dig("flash_settings", "flash_size"))
  loader = EspFlash::Loader.open(serial_port, chip: args.dig("extra_esptool_args", "chip"),
                                              baud: Integer(ENV.fetch("BAUD", "460800")),
                                              flash_size: flash_size)
  args["flash_files"].sort_by { |addr, _file| Integer(addr, 16) }.each do |addr, file|
    loader.write_flash(Integer(addr, 16), File.binread(File.join(__dir__, "build", file)), name: file)
  end
  loader.hard_reset
rescue EspFlash::Error => e
  abort "Error: #{e.message}"
ensure
  loader&.close
end

desc "Reset the board and show its console (quit with Ctrl-], reset with Ctrl-T Ctrl-R; NO_RESET=1 to attach as it is)"
task :monitor do
  desc_json = build_json("project_description.json")
  EspFlash::Monitor.new(serial_port, baud: Integer(desc_json["monitor_baud"]), target: desc_json["target"],
                                     elf: File.join(__dir__, "build", desc_json["app_elf"]),
                                     reset: ENV["NO_RESET"].to_s.empty?).run
rescue EspFlash::Error => e
  abort "Error: #{e.message}"
end

desc "Show the firmware's memory usage"
task :size do
  container "idf.py", "size"
end

desc "Open menuconfig"
task :menuconfig do
  container "idf.py", "menuconfig"
end

desc "Print the C that Spinel generates for lib/main.rb (or PROJECT=<dir>)"
task :c do
  container "spinel", "-I", "packages", ruby_main, "-S"
end

desc "Decode backtrace addresses: rake addr2line ADDRS='0x4200760a 0x4202c085'"
task :addr2line do
  addrs = ENV.fetch("ADDRS") { abort "set ADDRS to the backtrace's addresses" }.split
  target = configured_target or abort "no sdkconfig; run rake build first"
  prefix = %w[esp32 esp32s2 esp32s3].include?(target) ? "xtensa-#{target}-elf" : "riscv32-esp-elf"
  container "#{prefix}-addr2line", "-pfiaC", "-e", "build/espinel.elf", *addrs
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
