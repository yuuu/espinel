# One MQTT 3.1.1 control packet: its type, the four flag bits of the fixed
# header and the body after the remaining length. Building, encoding,
# reading and taking apart only; it holds no socket, so it serves a broker
# or a test as well as Client.
module Net
  module MQTT
    class Packet
      attr_reader :type, :flags, :body

      def initialize(type, flags, body)
        @type = type
        @flags = flags
        @body = body
      end

      # --- building

      def self.connect(client_id:, keep_alive:, clean_session:, username:, password:,
                       will_topic:, will_payload:, will_qos:, will_retain:)
        flags = 0
        flags |= 0x02 if clean_session
        payload = string(client_id)
        unless will_topic.nil?
          flags |= 0x04 | (will_qos << 3)
          flags |= 0x20 if will_retain
          payload += string(will_topic) + string(will_payload)
        end
        unless username.nil?
          flags |= 0x80
          payload += string(username)
          unless password.nil?
            flags |= 0x40
            payload += string(password)
          end
        end
        new(CONNECT, 0, string("MQTT") + [4, flags].pack("CC") + u16(keep_alive) + payload)
      end

      # id is ignored at QoS 0
      def self.publish(topic, payload, qos:, retain:, id:)
        body = string(topic)
        body += u16(id) if qos > 0
        new(PUBLISH, (retain ? 0x01 : 0) | (qos << 1), body + payload.to_s.b)
      end

      def self.subscribe(id, topics, qos)
        body = u16(id)
        topics.each { |t| body += string(t) + [qos].pack("C") }
        new(SUBSCRIBE, 0x02, body)
      end

      def self.unsubscribe(id, topics)
        body = u16(id)
        topics.each { |t| body += string(t) }
        new(UNSUBSCRIBE, 0x02, body)
      end

      def self.puback(id) = new(PUBACK, 0, u16(id))
      def self.pingreq = new(PINGREQ, 0, "".b)
      def self.disconnect = new(DISCONNECT, 0, "".b)

      # --- encoding

      def self.u16(n) = [n].pack("n")

      # a UTF-8 string, its byte count first
      def self.string(str)
        s = str.to_s.b
        u16(s.bytesize) + s
      end

      # the remaining length, 7 bits a byte
      def self.length(n)
        out = "".b
        loop do
          byte = n % 128
          n /= 128
          byte |= 0x80 if n > 0
          out += [byte].pack("C")
          break if n == 0
        end
        out
      end

      def encode
        [(@type << 4) | @flags].pack("C") + Packet.length(@body.bytesize) + @body
      end

      # --- reading: io answers read(n) with exactly n bytes (Connection does)

      def self.read(io)
        first = io.read(1).getbyte(0)
        length = 0
        multiplier = 1
        loop do
          byte = io.read(1).getbyte(0)
          length += (byte & 0x7F) * multiplier
          break if (byte & 0x80) == 0
          multiplier *= 128
          raise ProtocolError, "invalid remaining length" if multiplier > 128 * 128 * 128
        end
        new(first >> 4, first & 0x0F, length > 0 ? io.read(length) : "".b)
      end

      # --- taking apart

      # the packet identifier that starts the body of a PUBACK, SUBACK or UNSUBACK
      def packet_id = u16_at(0)

      # PUBLISH
      def qos = (@flags >> 1) & 0x03
      def retain? = (@flags & 0x01) != 0
      def topic = @body.byteslice(2, topic_bytes).force_encoding("UTF-8")
      def publish_id = qos > 0 ? u16_at(2 + topic_bytes) : 0

      def payload
        offset = 2 + topic_bytes + (qos > 0 ? 2 : 0)
        @body.byteslice(offset, @body.bytesize - offset).force_encoding("UTF-8")
      end

      # CONNACK
      def return_code
        raise ProtocolError, "invalid CONNACK" if @body.bytesize < 2
        @body.getbyte(1)
      end

      def refusal
        case return_code
        when CONNACK_REFUSED_PROTOCOL then "unacceptable protocol version"
        when CONNACK_REFUSED_IDENTIFIER then "identifier rejected"
        when CONNACK_REFUSED_SERVER then "server unavailable"
        when CONNACK_REFUSED_CREDENTIALS then "bad user name or password"
        when CONNACK_REFUSED_AUTHORIZED then "not authorized"
        else "connection refused: #{return_code}"
        end
      end

      # SUBACK: the index of the first topic the broker refused, or -1
      def refused_index
        i = 2
        while i < @body.bytesize
          return i - 2 if @body.getbyte(i) == 0x80
          i += 1
        end
        -1
      end

      private

      def topic_bytes = u16_at(0)
      def u16_at(i) = (@body.getbyte(i) << 8) | @body.getbyte(i + 1)
    end
  end
end
