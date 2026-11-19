require_relative "font5x7"

# A 128x64 SSD1306 OLED on I2C. Drawing goes to a frame buffer in memory
# (a binary String, one byte per column of eight pixels, as the display's
# RAM is laid out); show sends the whole of it.
class SSD1306
  WIDTH = 128
  HEIGHT = 64
  PAGES = HEIGHT / 8

  INIT = [
    0xae,       # display off
    0xd5, 0x80, # clock divide
    0xa8, 0x3f, # multiplex: 64 rows
    0xd3, 0x00, # no display offset
    0x40,       # start line 0
    0x8d, 0x14, # charge pump on
    0x20, 0x00, # horizontal addressing
    0xda, 0x12, # COM pins for 64 rows
    0x81, 0xcf, # contrast
    0xd9, 0xf1, # precharge
    0xdb, 0x40, # VCOMH
    0xa4, 0xa6, # show the RAM, not inverted
    0x2e,       # no scrolling
    0xaf        # display on
  ]

  attr_reader :buffer

  # upside_down: turns the picture by 180 degrees, for a display mounted
  # the other way round (pins at the bottom)
  def initialize(i2c, address = 0x3c, upside_down: false)
    @i2c = i2c
    @address = address
    @buffer = "\x00".b * (WIDTH * PAGES)
    # column and row order, before the display comes on: 0xa1, 0xc8 mirror
    # both, the right way up with the pins at the top
    command(*(upside_down ? [0xa0, 0xc0] : [0xa1, 0xc8]))
    command(*INIT)
  end

  def command(*bytes)
    @i2c.write(@address, 0x00, bytes)
  end

  def contrast(level)
    command(0x81, level)
  end

  def show
    command(0x21, 0, WIDTH - 1, 0x22, 0, PAGES - 1)
    @i2c.write(@address, 0x40, @buffer)
  end

  def clear
    @buffer.replace("\x00".b * (WIDTH * PAGES))
  end

  # A copy of what is drawn, for restore
  def snapshot
    @buffer.dup
  end

  def restore(snapshot)
    @buffer.replace(snapshot)
  end

  def set(x, y)
    return if x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT
    i = (y >> 3) * WIDTH + x
    @buffer.setbyte(i, @buffer.getbyte(i) | (1 << (y & 7)))
  end

  def flip(x, y)
    return if x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT
    i = (y >> 3) * WIDTH + x
    @buffer.setbyte(i, @buffer.getbyte(i) ^ (1 << (y & 7)))
  end

  def fill_rect(x, y, w, h)
    h.times { |dy| w.times { |dx| set(x + dx, y + dy) } }
  end

  def hline(x, y, w)
    fill_rect(x, y, w, 1)
  end

  def vline(x, y, h)
    fill_rect(x, y, 1, h)
  end

  # Draws str with its top left at x, y, each font pixel scale x scale;
  # columns at limit and beyond are left out.
  def text(str, x, y, scale: 1, limit: WIDTH)
    str.each_char do |c|
      code = c.ord
      code = 0x3f if code < 0x20 || code > 0x7e # ?
      base = (code - 0x20) * 5
      5.times do |col|
        px = x + col * scale
        next if px >= limit
        bits = FONT5X7[base + col]
        7.times do |row|
          fill_rect(px, y + row * scale, scale, scale) if (bits >> row) & 1 == 1
        end
      end
      x += 6 * scale
    end
  end

  def self.text_width(str, scale = 1)
    (str.size * 6 - 1) * scale
  end
end
