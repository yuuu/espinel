# An MQTT 3.1.1 client, with PicoRuby's interface (picoruby-net-mqtt's
# Net::MQTT::Client), in Ruby over Spinel's TCPSocket and, for TLS, the
# ssl_socket package. `require "net/mqtt"` reaches this file through
# `spinel -I packages`.
#
#   client = Net::MQTT::Client.new("test.mosquitto.org", 1883)
#   client.connect
#   client.subscribe("espinel/#")
#   client.publish("espinel/hello", "world")
#   topic, payload = client.receive(timeout: 5)
#   client.disconnect
#
# Beyond PicoRuby's: QoS 1 (publish waits for the PUBACK, a message
# received at QoS 1 is acknowledged), a last will, clean_session: false,
# and ssl_context: for certificates given as PEM. QoS 2 is not supported.
#
# TLS (ssl: true or ssl_context:) needs `require "ssl_socket"` in the
# program as well. Without it the TLS code is left out (Spinel folds
# `defined?(SSLSocket)` at compile time), which keeps Mbed TLS's 200 KB out
# of a program that does not use it.
#
# Thread is not supported here, so nothing runs in the background: a ping
# goes out only while the program is in a call of this client (receive
# sends them while it waits). A program that sleeps longer than keep_alive
# without calling it is disconnected by the broker.
require "socket"

module Net
  module MQTT
    class MQTTError < StandardError; end
    class ConnectionError < MQTTError; end
    class ProtocolError < MQTTError; end

    # Control packet types
    CONNECT     = 1
    CONNACK     = 2
    PUBLISH     = 3
    PUBACK      = 4
    PUBREC      = 5
    PUBREL      = 6
    PUBCOMP     = 7
    SUBSCRIBE   = 8
    SUBACK      = 9
    UNSUBSCRIBE = 10
    UNSUBACK    = 11
    PINGREQ     = 12
    PINGRESP    = 13
    DISCONNECT  = 14

    # CONNACK return codes
    CONNACK_ACCEPTED            = 0x00
    CONNACK_REFUSED_PROTOCOL    = 0x01
    CONNACK_REFUSED_IDENTIFIER  = 0x02
    CONNACK_REFUSED_SERVER      = 0x03
    CONNACK_REFUSED_CREDENTIALS = 0x04
    CONNACK_REFUSED_AUTHORIZED  = 0x05

    class Client
      attr_reader :host, :port
      attr_accessor :client_id, :keep_alive, :clean_session, :username, :password
      attr_accessor :ssl, :ca_file, :cert_file, :key_file, :ssl_context
      attr_accessor :will_topic, :will_payload, :will_qos, :will_retain
      # seconds to wait for the broker's answer to a CONNECT, SUBSCRIBE,
      # UNSUBSCRIBE, PINGREQ or a QoS 1 PUBLISH
      attr_accessor :ack_timeout

      def self.connect(host, port = 1883, **options)
        client = new(host, port, **options)
        client.connect
        return client unless block_given?
        begin
          yield client
        ensure
          client.disconnect
        end
      end

      def initialize(host, port = 1883, client_id: nil, keep_alive: 60, clean_session: true,
                     username: nil, password: nil,
                     ssl: false, ca_file: nil, cert_file: nil, key_file: nil, ssl_context: nil,
                     will_topic: nil, will_payload: "", will_qos: 0, will_retain: false,
                     ack_timeout: 30)
        @host = host
        @port = port
        @client_id = client_id || "espinel-#{rand(1_000_000_000)}"
        @keep_alive = keep_alive
        @clean_session = clean_session
        @username = username
        @password = password
        @ssl = ssl
        @ca_file = ca_file
        @cert_file = cert_file
        @key_file = key_file
        @ssl_context = ssl_context
        @will_topic = will_topic
        @will_payload = will_payload
        @will_qos = will_qos
        @will_retain = will_retain
        @ack_timeout = ack_timeout
        @tcp = nil
        @tls = nil
        @packet_id = 0
        @inbox = []
        @last_sent = 0.0
        @pingresp_due = nil
        @last_data = "".b
      end

      # The keywords, as PicoRuby's README gives them, override the ones given to new.
      def connect(client_id: nil, keep_alive: nil, clean_session: nil, username: nil, password: nil)
        raise MQTTError, "already connected" if connected?
        @client_id = client_id if client_id
        @keep_alive = keep_alive if keep_alive
        @clean_session = clean_session unless clean_session.nil?
        @username = username if username
        @password = password if password
        open_socket
        ok = false
        begin
          send_connect
          data = wait_for(CONNACK, -1)
          raise ProtocolError, "invalid CONNACK" if data.bytesize < 2
          code = data.getbyte(1)
          raise ConnectionError, connack_message(code) if code != CONNACK_ACCEPTED
          ok = true
        ensure
          close_socket unless ok
        end
        true
      end

      def connected?
        !@tcp.nil?
      end

      def publish(topic, payload, retain: false, qos: 0)
        check_connected
        raise MQTTError, "QoS must be 0 or 1" unless qos == 0 || qos == 1
        body = encode_string(topic)
        id = 0
        if qos == 1
          id = next_packet_id
          body += u16(id)
        end
        body += payload.to_s.b
        flags = (retain ? 0x01 : 0) | (qos << 1)
        write_packet(PUBLISH, flags, body)
        wait_for(PUBACK, id) if qos == 1
        true
      end

      def subscribe(*topics, qos: 0)
        check_connected
        raise MQTTError, "QoS must be 0 or 1" unless qos == 0 || qos == 1
        id = next_packet_id
        body = u16(id)
        topics.each { |t| body += encode_string(t) + [qos].pack("C") }
        write_packet(SUBSCRIBE, 0x02, body)
        data = wait_for(SUBACK, id)
        i = 2
        while i < data.bytesize
          raise MQTTError, "subscription to #{topics[i - 2]} refused" if data.getbyte(i) == 0x80
          i += 1
        end
        true
      end

      def unsubscribe(*topics)
        check_connected
        id = next_packet_id
        body = u16(id)
        topics.each { |t| body += encode_string(t) }
        write_packet(UNSUBSCRIBE, 0x02, body)
        wait_for(UNSUBACK, id)
        true
      end

      # The next message, [topic, payload], waiting up to timeout seconds
      # (nil: as long as it takes); nil when none came. Pings go out while
      # it waits.
      def receive(timeout: nil)
        check_connected
        deadline = timeout.nil? ? nil : now + timeout
        loop do
          return @inbox.shift unless @inbox.empty?
          wait = keepalive_wait
          unless deadline.nil?
            left = deadline - now
            return nil if left <= 0
            wait = left if wait.nil? || left < wait
          end
          if readable?(wait)
            read_and_handle
          else
            keepalive
          end
        end
      end

      def ping
        check_connected
        write_packet(PINGREQ, 0, "".b)
        wait_for(PINGRESP, -1)
        true
      end

      def disconnect
        return nil unless connected?
        begin
          write_packet(DISCONNECT, 0, "".b)
        ensure
          close_socket
        end
        nil
      end

      private

      def now
        Process.clock_gettime(Process::CLOCK_MONOTONIC)
      end

      def check_connected
        raise MQTTError, "not connected" unless connected?
      end

      # Every use of SSLSocket and @tls sits under defined?(SSLSocket), so
      # that a program without ssl_socket compiles none of it.
      def tls?
        return false unless defined?(SSLSocket)
        !@tls.nil?
      end

      def open_socket
        tls_wanted = @ssl || !@ssl_context.nil?
        if tls_wanted && !defined?(SSLSocket)
          raise MQTTError, "TLS needs require \"ssl_socket\" in the program"
        end
        tcp = TCPSocket.new(@host, @port)
        @tcp = tcp
        return unless tls_wanted
        if defined?(SSLSocket)
          ok = false
          begin
            tls = SSLSocket.new(tcp, @ssl_context || build_ssl_context)
            tls.hostname = @host
            tls.connect
            @tls = tls
            ok = true
          ensure
            close_socket unless ok
          end
        end
      end

      def build_ssl_context
        ctx = SSLContext.new
        ctx.ca_file = @ca_file if @ca_file
        ctx.cert_file = @cert_file if @cert_file
        ctx.key_file = @key_file if @key_file
        ctx
      end

      def close_socket
        tcp = @tcp
        @tcp = nil
        @pingresp_due = nil
        if defined?(SSLSocket)
          tls = @tls
          @tls = nil
          if tls
            tls.close
            return
          end
        end
        tcp.close if tcp && !tcp.closed?
      end

      def next_packet_id
        @packet_id = (@packet_id + 1) & 0xFFFF
        @packet_id = 1 if @packet_id == 0
        @packet_id
      end

      def connack_message(code)
        case code
        when CONNACK_REFUSED_PROTOCOL then "unacceptable protocol version"
        when CONNACK_REFUSED_IDENTIFIER then "identifier rejected"
        when CONNACK_REFUSED_SERVER then "server unavailable"
        when CONNACK_REFUSED_CREDENTIALS then "bad user name or password"
        when CONNACK_REFUSED_AUTHORIZED then "not authorized"
        else "connection refused: #{code}"
        end
      end

      # --- encoding

      def u16(n)
        [n].pack("n")
      end

      def encode_string(str)
        s = str.to_s.b
        u16(s.bytesize) + s
      end

      def encode_length(length)
        out = "".b
        loop do
          byte = length % 128
          length /= 128
          byte |= 0x80 if length > 0
          out += [byte].pack("C")
          break if length == 0
        end
        out
      end

      def send_connect
        flags = 0
        flags |= 0x02 if @clean_session
        payload = encode_string(@client_id)
        will = @will_topic
        unless will.nil?
          flags |= 0x04 | (@will_qos << 3)
          flags |= 0x20 if @will_retain
          payload += encode_string(will) + encode_string(@will_payload)
        end
        user = @username
        unless user.nil?
          flags |= 0x80
          payload += encode_string(user)
          pass = @password
          unless pass.nil?
            flags |= 0x40
            payload += encode_string(pass)
          end
        end
        header = encode_string("MQTT") + [4, flags].pack("CC") + u16(@keep_alive)
        write_packet(CONNECT, 0, header + payload)
      end

      # --- I/O

      def write_packet(type, flags, body)
        packet = [(type << 4) | flags].pack("C") + encode_length(body.bytesize) + body
        if defined?(SSLSocket) && tls?
          @tls.write(packet)
        else
          @tcp.write(packet)
        end
        @last_sent = now
      end

      # exactly n bytes; ConnectionError (and disconnected) when the broker closed
      def read_bytes(n)
        data = nil
        if defined?(SSLSocket) && tls?
          data = @tls.read(n)
        else
          data = @tcp.read(n)
        end
        if data.nil? || data.bytesize < n
          close_socket
          raise ConnectionError, "connection closed by the broker"
        end
        data
      end

      # whether a packet can be read within seconds (nil: block)
      def readable?(seconds)
        if defined?(SSLSocket)
          return true if tls? && @tls.ready?
        end
        !IO.select([@tcp], nil, nil, seconds).nil?
      end

      # Reads one packet, keeps a PUBLISH for receive (acknowledging QoS 1)
      # and notes a PINGRESP. Answers the packet's type; its body is left
      # in @last_data.
      def read_and_handle
        first = read_bytes(1).getbyte(0)
        length = 0
        multiplier = 1
        loop do
          byte = read_bytes(1).getbyte(0)
          length += (byte & 0x7F) * multiplier
          break if (byte & 0x80) == 0
          multiplier *= 128
          raise ProtocolError, "invalid remaining length" if multiplier > 128 * 128 * 128
        end
        data = length > 0 ? read_bytes(length) : "".b
        type = first >> 4
        if type == PUBLISH
          qos = (first >> 1) & 0x03
          topic_length = (data.getbyte(0) << 8) | data.getbyte(1)
          topic = data.byteslice(2, topic_length).force_encoding("UTF-8")
          offset = 2 + topic_length
          if qos > 0
            id = (data.getbyte(offset) << 8) | data.getbyte(offset + 1)
            offset += 2
            write_packet(PUBACK, 0, u16(id)) if qos == 1
          end
          payload = data.byteslice(offset, data.bytesize - offset).force_encoding("UTF-8")
          @inbox << [topic, payload]
        elsif type == PINGRESP
          @pingresp_due = nil
        end
        @last_data = data
        type
      end

      # Reads until a packet of type comes (with the packet identifier id,
      # unless id is -1) and answers its body. Messages that come first
      # wait for receive.
      def wait_for(type, id)
        deadline = now + @ack_timeout
        loop do
          left = deadline - now
          raise ConnectionError, "no answer from the broker in #{@ack_timeout} s" if left <= 0
          next unless readable?(left)
          next unless read_and_handle == type
          data = @last_data
          return data if id < 0 || ((data.getbyte(0) << 8) | data.getbyte(1)) == id
        end
      end

      # seconds until a ping is due or its answer overdue; nil without keep-alive
      def keepalive_wait
        return nil if @keep_alive <= 0
        due = @pingresp_due
        t = (due.nil? ? @last_sent + @keep_alive * 0.75 : due) - now
        t < 0 ? 0.0 : t
      end

      # Sends a ping when one is due; disconnects when the broker did not answer the last
      def keepalive
        return if @keep_alive <= 0
        due = @pingresp_due
        if due.nil?
          return if now < @last_sent + @keep_alive * 0.75
          write_packet(PINGREQ, 0, "".b)
          @pingresp_due = now + @keep_alive
        elsif now >= due
          close_socket
          raise ConnectionError, "no PINGRESP from the broker"
        end
      end
    end
  end
end
