require "i2c"
require "machine"
require_relative "ssd1306"

# "Spinel on ESP32" on a 128x64 SSD1306 OLED: a scan line draws the title,
# an underline grows from the middle, the subtitle types itself, and then
# a glint sweeps across the title while the display breathes.
#
# Wiring (Seeed Studio XIAO ESP32S3): the OLED's SDA to D4 (GPIO5), SCL to
# D5 (GPIO6), VCC to 3V3 and GND to GND; the display is at 0x3C.
#
#   bundle exec rake flash monitor PROJECT=examples/oled
TITLE = "Spinel"
TITLE_SCALE = 3
TITLE_W = SSD1306.text_width(TITLE, TITLE_SCALE)
TITLE_X = (SSD1306::WIDTH - TITLE_W) / 2
TITLE_Y = 3
TITLE_H = 7 * TITLE_SCALE

LINE_Y = TITLE_Y + TITLE_H + 4

SUB = "on ESP32"
SUB_SCALE = 2
SUB_X = (SSD1306::WIDTH - SSD1306.text_width(SUB, SUB_SCALE)) / 2
SUB_Y = LINE_Y + 5

FOOT = "Ruby, compiled AOT"
FOOT_X = (SSD1306::WIDTH - SSD1306.text_width(FOOT)) / 2
FOOT_Y = SSD1306::HEIGHT - 7

# A beam moves left to right; the title appears behind it.
def scan_in(oled)
  x = TITLE_X - 4
  while x <= TITLE_X + TITLE_W + 4
    oled.clear
    oled.text(TITLE, TITLE_X, TITLE_Y, scale: TITLE_SCALE, limit: x)
    oled.vline(x, TITLE_Y - 2, TITLE_H + 4)
    oled.vline(x + 2, TITLE_Y + 4, TITLE_H - 8)
    oled.show
    x += 3
  end
  oled.clear
  oled.text(TITLE, TITLE_X, TITLE_Y, scale: TITLE_SCALE)
  oled.show
end

# The underline grows out from the middle.
def underline(oled)
  base = oled.snapshot
  half = 0
  while half <= 60
    oled.restore(base)
    oled.hline(64 - half, LINE_Y, half * 2)
    oled.show
    half += 4
  end
end

# The subtitle types itself, a block cursor ahead of it.
def type_in(oled, str, x, y, scale, delay)
  base = oled.snapshot
  (str.size + 1).times do |n|
    oled.restore(base)
    oled.text(str[0, n], x, y, scale: scale)
    oled.fill_rect(x + n * 6 * scale, y, 5 * scale, 7 * scale) if n < str.size
    oled.show
    Machine.sleep(delay)
  end
end

# A slanted band of inverted pixels sweeps across the title.
def glint(oled, base)
  pos = TITLE_X - TITLE_H
  while pos <= TITLE_X + TITLE_W
    oled.restore(base)
    TITLE_H.times do |dy|
      y = TITLE_Y + dy
      x0 = pos + (TITLE_H - dy) / 2
      6.times { |dx| oled.flip(x0 + dx, y) if x0 + dx >= TITLE_X && x0 + dx < TITLE_X + TITLE_W }
    end
    oled.show
    pos += 4
  end
  oled.restore(base)
  oled.show
end

# The contrast falls and rises once.
def breathe(oled)
  [0xcf, 0x90, 0x50, 0x20, 0x08, 0x20, 0x50, 0x90, 0xcf].each do |level|
    oled.contrast(level)
    Machine.sleep(0.08)
  end
end

i2c = I2C.new(frequency: 400_000, scl_pin: 6, sda_pin: 5)
# on a breadboard the OLED's pins point down: turn the picture over
oled = SSD1306.new(i2c, upside_down: true)

scan_in(oled)
underline(oled)
type_in(oled, SUB, SUB_X, SUB_Y, SUB_SCALE, 0.07)
type_in(oled, FOOT, FOOT_X, FOOT_Y, 1, 0.02)
puts "Spinel on ESP32"

final = oled.snapshot
loop do
  glint(oled, final)
  Machine.sleep(1.5)
  breathe(oled)
  Machine.sleep(1.5)
end
