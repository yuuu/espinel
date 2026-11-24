# Spinel's bundled packages compiled into the firmware: json and base64.
require "json"
require "base64"

data = { "name" => "espinel", "pins" => [1, 2, 3], "ok" => true, "none" => nil }
s = JSON.generate(data)
puts s
back = JSON.parse(s)
puts back["pins"].sum
puts back == data
puts Base64.encode64("hello, world")
puts Base64.decode64("aGVsbG8sIHdvcmxk")
