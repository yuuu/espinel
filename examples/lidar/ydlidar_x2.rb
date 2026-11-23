# A YDLIDAR X2 on a UART. The X2 has no commands: once powered it spins
# and streams packets at 115200 baud, 8N1. Each packet is
#
#   PH (0xAA 0x55)  CT  LSN  FSA (2)  LSA (2)  CS (2)  LSN samples (2 each)
#
# little-endian. CT bit 0 marks the packet at the start of a rotation (its
# upper bits are the scan frequency in 0.1 Hz); FSA and LSA are the angles
# of the first and last sample (bits 15-1, in 1/64 degree); a sample is
# the distance in 1/4 mm (0: nothing measured). CS is the XOR of every
# other 16-bit word of the packet.
class YDLidarX2
  BAUDRATE = 115_200
  # the X2's triangulation: the angle a sample at distance d (mm) is off by
  ANGLE_K = 21.8
  ANGLE_D = 155.3
  RAD_TO_DEG = 180.0 / Math::PI

  # Hz, from the last start packet (0.0 until one has come)
  attr_reader :frequency
  # packets dropped for a bad checksum
  attr_reader :errors

  def initialize(uart)
    @uart = uart
    @frequency = 0.0
    @errors = 0
  end

  # Reads packets until the next rotation begins, keeping in ranges (an
  # Array of 360 Integers, one per degree clockwise from the front, seen
  # from above) the distance in mm of the last sample at each degree; a
  # degree that measured nothing is left as it was. Answers the number of
  # samples taken.
  def scan(ranges)
    count = 0
    loop do
      sync
      head = @uart.read(8)
      ct = head.getbyte(0)
      lsn = head.getbyte(1)
      fsa = word(head, 2)
      lsa = word(head, 4)
      cs = word(head, 6)
      data = @uart.read(lsn * 2)

      sum = 0x55aa ^ fsa ^ lsa ^ (ct | (lsn << 8))
      lsn.times { |i| sum ^= word(data, i * 2) }
      if sum != cs
        @errors += 1
        next
      end

      if ct & 1 == 1
        @frequency = (ct >> 1) / 10.0
        return count if count > 0
        next
      end
      next if lsn == 0

      first = (fsa >> 1) / 64.0
      last = (lsa >> 1) / 64.0
      span = last - first
      span += 360.0 if span < 0
      step = lsn > 1 ? span / (lsn - 1) : 0.0
      lsn.times do |i|
        dist = word(data, i * 2) / 4.0
        next if dist <= 0
        angle = first + step * i + correction(dist)
        deg = angle.round % 360
        ranges[deg] = dist.round
        count += 1
      end
    end
  end

  private

  # Waits for the packet header, 0xAA 0x55.
  def sync
    byte = @uart.read(1).getbyte(0)
    loop do
      byte = @uart.read(1).getbyte(0) while byte != 0xaa
      byte = @uart.read(1).getbyte(0)
      return if byte == 0x55
    end
  end

  def word(str, i)
    str.getbyte(i) | (str.getbyte(i + 1) << 8)
  end

  def correction(dist)
    Math.atan(ANGLE_K * (ANGLE_D - dist) / (ANGLE_D * dist)) * RAD_TO_DEG
  end
end
