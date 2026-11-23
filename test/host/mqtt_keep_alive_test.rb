# Net::MQTT::KeepAlive on a clock the test moves: when a PINGREQ is due,
# when its PINGRESP is overdue, and how long receive may wait meanwhile.
require "net/mqtt"
require_relative "support/assert"

$now = 1000.0

module Net
  module MQTT
    def self.now = $now
  end
end

def at(t) = $now = t

test("a ping is due three quarters of the keep-alive after the last packet sent") do
  at 1000.0
  timer = Net::MQTT::KeepAlive.new(60)
  assert_equal 45.0, timer.wait
  at 1044.9
  assert !timer.ping_due?, "not yet at 44.9 s"
  at 1045.0
  assert timer.ping_due?, "due at 45 s"
  assert_equal 0.0, timer.wait
  at 1100.0
  assert_equal 0.0, timer.wait, "never negative"
end

test("sending a packet puts the ping off") do
  at 1000.0
  timer = Net::MQTT::KeepAlive.new(60)
  at 1040.0
  timer.sent
  at 1045.0
  assert !timer.ping_due?, "put off by the packet at 40 s"
  assert_equal 40.0, timer.wait
  at 1085.0
  assert timer.ping_due?, "due 45 s after the packet"
end

test("a PINGRESP is overdue one keep-alive after the ping") do
  at 1000.0
  timer = Net::MQTT::KeepAlive.new(60)
  at 1045.0
  timer.ping_sent
  assert !timer.ping_due?, "no second ping while one is unanswered"
  assert_equal 60.0, timer.wait
  at 1104.9
  assert !timer.pingresp_overdue?, "not overdue at 59.9 s"
  at 1105.0
  assert timer.pingresp_overdue?, "overdue at 60 s"
end

test("a PINGRESP clears the wait for it") do
  at 1000.0
  timer = Net::MQTT::KeepAlive.new(60)
  at 1045.0
  timer.ping_sent
  timer.sent
  at 1050.0
  timer.pingresp_received
  assert !timer.pingresp_overdue?, "answered"
  assert_equal 40.0, timer.wait, "the next ping counts from the PINGREQ sent"
end

test("keep-alive 0 never pings") do
  at 1000.0
  timer = Net::MQTT::KeepAlive.new(0)
  at 1_000_000.0
  assert !timer.ping_due?, "never due"
  assert !timer.pingresp_overdue?, "never overdue"
  assert timer.wait.nil?, "no wait"
end

finish
