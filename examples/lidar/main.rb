require "uart"
require "i2c"
require_relative "ydlidar_x2"
require_relative "../oled/ssd1306"

# What a YDLIDAR X2 sees around it, drawn on a 128x64 SSD1306 OLED as a
# radar: the left square is a map of one rotation seen from above, the
# LiDAR at its middle and its front up, with a ring for each metre; the
# right half gives the scan frequency, the points in the rotation and the
# nearest one. The console gets a line about once a second.
#
# Wiring (Seeed Studio XIAO ESP32S3):
#   X2 VCC to 5V, GND to GND, TX to D0 (GPIO1); M_CTR left open (the
#   default speed)
#   OLED SDA to D4 (GPIO5), SCL to D5 (GPIO6), VCC to 3V3, GND to GND; at
#   0x3C
#
#   bundle exec rake flash monitor PROJECT=examples/lidar
RANGE_MM = 3000 # the map's edge
CX = 31
CY = 32
RADIUS = 31
PANEL_X = 68

# The direction of each degree on the screen, clockwise from straight up.
SIN = Array.new(360) { |d| Math.sin(d * Math::PI / 180.0) }
COS = Array.new(360) { |d| Math.cos(d * Math::PI / 180.0) }

def plot(oled, deg, mm)
  r = mm * RADIUS / RANGE_MM
  oled.set(CX + (r * SIN[deg]).round, CY - (r * COS[deg]).round)
end

# "1.23m"
def metres(mm)
  cm = (mm + 5) / 10
  frac = cm % 100
  "#{cm / 100}.#{frac < 10 ? "0" : ""}#{frac}m"
end

def draw(oled, ranges, lidar, points)
  oled.clear

  # a ring of dots every metre, the LiDAR and its front
  m = 1000
  while m <= RANGE_MM
    0.step(359, 10) { |deg| plot(oled, deg, m) }
    m += 1000
  end
  oled.fill_rect(CX - 1, CY - 1, 3, 3)
  oled.vline(CX, CY - 5, 3)

  nearest = 0
  nearest_deg = 0
  360.times do |deg|
    mm = ranges[deg]
    next if mm == 0
    plot(oled, deg, mm) if mm <= RANGE_MM
    if nearest == 0 || mm < nearest
      nearest = mm
      nearest_deg = deg
    end
  end

  oled.vline(PANEL_X - 3, 0, SSD1306::HEIGHT)
  oled.text("X2 LiDAR", PANEL_X, 0)
  hz = (lidar.frequency * 10).round
  oled.text("#{hz / 10}.#{hz % 10} Hz", PANEL_X, 12)
  oled.text("#{points} pts", PANEL_X, 22)
  oled.text("nearest", PANEL_X, 36)
  if nearest > 0
    oled.text(metres(nearest), PANEL_X, 46)
    oled.text("#{nearest_deg} deg", PANEL_X, 56)
    # a line out to the nearest point, if it is on the map
    if nearest <= RANGE_MM
      r = nearest * RADIUS / RANGE_MM
      4.times do |i|
        q = r * (i + 1) / 5
        oled.set(CX + (q * SIN[nearest_deg]).round, CY - (q * COS[nearest_deg]).round)
      end
    end
  else
    oled.text("-", PANEL_X, 46)
  end
  oled.show
  [nearest, nearest_deg]
end

uart = UART.new(1, baudrate: YDLidarX2::BAUDRATE, rxd_pin: 1)
i2c = I2C.new(frequency: 400_000, scl_pin: 6, sda_pin: 5)
# on a breadboard the OLED's pins point down: turn the picture over
oled = SSD1306.new(i2c, upside_down: true)
oled.text("waiting for X2...", 0, 28)
oled.show

lidar = YDLidarX2.new(uart)
ranges = Array.new(360, 0)
rotations = 0
loop do
  points = lidar.scan(ranges)
  nearest, deg = draw(oled, ranges, lidar, points)
  rotations += 1
  if rotations % 7 == 0
    puts "#{lidar.frequency} Hz, #{points} points, nearest #{metres(nearest)} at #{deg} deg, #{lidar.errors} bad packets"
  end
  360.times { |d| ranges[d] = 0 }
end
