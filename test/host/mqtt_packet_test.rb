# Net::MQTT::Packet: encoding and reading MQTT 3.1.1 packets, byte for byte
# against the specification, with no socket.
require "net/mqtt"
require_relative "support/assert"

include Net::MQTT

# what Packet.read reads from: read(n) answers the next n bytes
class ByteReader
  def initialize(bytes)
    @bytes = bytes.b
    @pos = 0
  end

  def read(n)
    raise EOFError, "read past the end" if @pos + n > @bytes.bytesize
    s = @bytes.byteslice(@pos, n)
    @pos += n
    s
  end

  def rest = @bytes.bytesize - @pos
end

def bytes(*list) = list.pack("C*")

def decode(encoded) = Packet.read(ByteReader.new(encoded))

test("remaining length takes 1 to 4 bytes, 7 bits each") do
  assert_equal bytes(0x00), Packet.length(0)
  assert_equal bytes(0x7f), Packet.length(127)
  assert_equal bytes(0x80, 0x01), Packet.length(128)
  assert_equal bytes(0xff, 0x7f), Packet.length(16_383)
  assert_equal bytes(0x80, 0x80, 0x01), Packet.length(16_384)
  assert_equal bytes(0xff, 0xff, 0x7f), Packet.length(2_097_151)
  assert_equal bytes(0x80, 0x80, 0x80, 0x01), Packet.length(2_097_152)
  assert_equal bytes(0xff, 0xff, 0xff, 0x7f), Packet.length(268_435_455)
end

test("a packet read back is the packet encoded, at every length boundary") do
  [0, 1, 127, 128, 16_383, 16_384].each do |n|
    body = "x".b * n
    io = ByteReader.new(Packet.new(PUBLISH, 0x03, body).encode)
    packet = Packet.read(io)
    assert_equal PUBLISH, packet.type, "type at #{n}"
    assert_equal 0x03, packet.flags, "flags at #{n}"
    assert_equal n, packet.body.bytesize, "body length at #{n}"
    assert_equal 0, io.rest, "bytes left after #{n}"
  end
end

test("a remaining length longer than 4 bytes is refused") do
  assert_raises(ProtocolError) { decode(bytes(0x30, 0x80, 0x80, 0x80, 0x80, 0x01)) }
end

test("CONNECT, the minimal one") do
  packet = Packet.connect(client_id: "abc", keep_alive: 60, clean_session: true,
                          username: nil, password: nil, will_topic: nil,
                          will_payload: "", will_qos: 0, will_retain: false)
  expected = bytes(0x10, 15, 0, 4) + "MQTT" + bytes(4, 0x02, 0, 60, 0, 3) + "abc"
  assert_equal expected, packet.encode
end

test("CONNECT with a will, a user name and a password") do
  packet = Packet.connect(client_id: "c", keep_alive: 300, clean_session: false,
                          username: "u", password: "pw", will_topic: "w/t",
                          will_payload: "bye", will_qos: 1, will_retain: true)
  # will 0x04, will QoS 1 0x08, will retain 0x20, password 0x40, user 0x80
  payload = bytes(0, 1) + "c" + bytes(0, 3) + "w/t" + bytes(0, 3) + "bye" +
            bytes(0, 1) + "u" + bytes(0, 2) + "pw"
  body = bytes(0, 4) + "MQTT" + bytes(4, 0xec, 0x01, 0x2c) + payload
  assert_equal bytes(0x10, body.bytesize) + body, packet.encode
end

test("CONNECT leaves out a password given without a user name") do
  packet = Packet.connect(client_id: "c", keep_alive: 0, clean_session: true,
                          username: nil, password: "pw", will_topic: nil,
                          will_payload: "", will_qos: 0, will_retain: false)
  assert_equal 0x02, packet.body.getbyte(7)
  assert_equal 13, packet.body.bytesize
end

test("PUBLISH at QoS 0 has no packet identifier") do
  packet = Packet.publish("a/b", "hi", qos: 0, retain: true, id: 9)
  assert_equal bytes(0x31, 7, 0, 3) + "a/b" + "hi", packet.encode
  back = decode(packet.encode)
  assert_equal "a/b", back.topic
  assert_equal "hi", back.payload
  assert_equal 0, back.qos
  assert back.retain?, "retain"
  assert_equal 0, back.publish_id
end

test("PUBLISH at QoS 1 carries its packet identifier") do
  packet = Packet.publish("t", "payload", qos: 1, retain: false, id: 0x1234)
  assert_equal bytes(0x32, 12, 0, 1) + "t" + bytes(0x12, 0x34) + "payload", packet.encode
  back = decode(packet.encode)
  assert_equal 1, back.qos
  assert !back.retain?, "no retain"
  assert_equal 0x1234, back.publish_id
  assert_equal "t", back.topic
  assert_equal "payload", back.payload
end

test("PUBLISH counts a topic's length in bytes") do
  back = decode(Packet.publish("温度/室内", "21.5℃", qos: 0, retain: false, id: 0).encode)
  assert_equal 13, back.body.getbyte(1)
  assert_equal "温度/室内", back.topic
  assert_equal "21.5℃", back.payload
end

test("PUBLISH with an empty payload") do
  back = decode(Packet.publish("t", "", qos: 1, retain: false, id: 1).encode)
  assert_equal "", back.payload
  assert_equal 1, back.publish_id
end

test("SUBSCRIBE and UNSUBSCRIBE set the reserved flag bit 1") do
  sub = Packet.subscribe(10, ["a", "b/#"], 1)
  assert_equal bytes(0x82, 12, 0, 10, 0, 1) + "a" + bytes(1, 0, 3) + "b/#" + bytes(1), sub.encode
  unsub = Packet.unsubscribe(11, ["a"])
  assert_equal bytes(0xa2, 5, 0, 11, 0, 1) + "a", unsub.encode
end

test("PUBACK, PINGREQ and DISCONNECT") do
  assert_equal bytes(0x40, 2, 0x01, 0x02), Packet.puback(0x0102).encode
  assert_equal bytes(0xc0, 0), Packet.pingreq.encode
  assert_equal bytes(0xe0, 0), Packet.disconnect.encode
end

test("SUBACK: the packet identifier and the first refused topic") do
  ok = decode(bytes(0x90, 4, 0, 5, 0, 1))
  assert_equal SUBACK, ok.type
  assert_equal 5, ok.packet_id
  assert_equal(-1, ok.refused_index)
  refused = decode(bytes(0x90, 5, 0, 6, 1, 0x80, 0x80))
  assert_equal 1, refused.refused_index
end

test("CONNACK: accepted, and each refusal") do
  assert_equal CONNACK_ACCEPTED, decode(bytes(0x20, 2, 0, 0)).return_code
  assert_equal "unacceptable protocol version", decode(bytes(0x20, 2, 0, 1)).refusal
  assert_equal "identifier rejected", decode(bytes(0x20, 2, 0, 2)).refusal
  assert_equal "server unavailable", decode(bytes(0x20, 2, 0, 3)).refusal
  assert_equal "bad user name or password", decode(bytes(0x20, 2, 0, 4)).refusal
  assert_equal "not authorized", decode(bytes(0x20, 2, 0, 5)).refusal
  assert_equal "connection refused: 6", decode(bytes(0x20, 2, 0, 6)).refusal
end

test("a CONNACK too short raises ProtocolError") do
  assert_raises(ProtocolError) { decode(bytes(0x20, 1, 0)).return_code }
end

finish
