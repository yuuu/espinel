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
  end
end

# Client stands on these: Packet (building and taking apart packets),
# Connection (TCP or TLS to the broker) and KeepAlive (when to ping).
require_relative "packet"
require_relative "connection"
require_relative "keep_alive"

module Net
  module MQTT
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
        @conn = nil
        @timer = KeepAlive.new(0)
        @packet_id = 0
        @inbox = []
      end

      # The keywords, as PicoRuby's README gives them, override the ones given to new.
      def connect(client_id: nil, keep_alive: nil, clean_session: nil, username: nil, password: nil)
        raise MQTTError, "already connected" if connected?
        @client_id = client_id if client_id
        @keep_alive = keep_alive if keep_alive
        @clean_session = clean_session unless clean_session.nil?
        @username = username if username
        @password = password if password
        @conn = Connection.new(@host, @port, tls_context)
        @timer = KeepAlive.new(@keep_alive)
        ok = false
        begin
          send_packet(Packet.connect(client_id: @client_id, keep_alive: @keep_alive,
                                     clean_session: @clean_session,
                                     username: @username, password: @password,
                                     will_topic: @will_topic, will_payload: @will_payload,
                                     will_qos: @will_qos, will_retain: @will_retain))
          connack = wait_for(CONNACK, -1)
          raise ConnectionError, connack.refusal if connack.return_code != CONNACK_ACCEPTED
          ok = true
        ensure
          drop unless ok
        end
        true
      end

      def connected?
        conn = @conn
        !conn.nil? && !conn.closed?
      end

      def publish(topic, payload, retain: false, qos: 0)
        check_connected
        check_qos(qos)
        id = qos > 0 ? next_packet_id : 0
        send_packet(Packet.publish(topic, payload, qos: qos, retain: retain, id: id))
        wait_for(PUBACK, id) if qos == 1
        true
      end

      def subscribe(*topics, qos: 0)
        check_connected
        check_qos(qos)
        id = next_packet_id
        send_packet(Packet.subscribe(id, topics, qos))
        i = wait_for(SUBACK, id).refused_index
        raise MQTTError, "subscription to #{topics[i]} refused" if i >= 0
        true
      end

      def unsubscribe(*topics)
        check_connected
        id = next_packet_id
        send_packet(Packet.unsubscribe(id, topics))
        wait_for(UNSUBACK, id)
        true
      end

      # The next message, [topic, payload], waiting up to timeout seconds
      # (nil: as long as it takes); nil when none came. Pings go out while
      # it waits.
      def receive(timeout: nil)
        check_connected
        deadline = timeout.nil? ? nil : MQTT.now + timeout
        loop do
          return @inbox.shift unless @inbox.empty?
          wait = @timer.wait
          unless deadline.nil?
            left = deadline - MQTT.now
            return nil if left <= 0
            wait = left if wait.nil? || left < wait
          end
          if @conn.readable?(wait)
            read_packet
          else
            ping_if_due
          end
        end
      end

      def ping
        check_connected
        send_packet(Packet.pingreq)
        wait_for(PINGRESP, -1)
        true
      end

      def disconnect
        return nil unless connected?
        begin
          send_packet(Packet.disconnect)
        ensure
          drop
        end
        nil
      end

      private

      def check_connected
        raise MQTTError, "not connected" unless connected?
      end

      def check_qos(qos)
        raise MQTTError, "QoS must be 0 or 1" unless qos == 0 || qos == 1
      end

      # the SSLContext to connect with, nil for plain TCP
      def tls_context
        return nil unless @ssl || !@ssl_context.nil?
        raise MQTTError, "TLS needs require \"ssl_socket\" in the program" unless defined?(SSLSocket)
        if defined?(SSLSocket)
          ctx = @ssl_context
          return ctx unless ctx.nil?
          ctx = SSLContext.new
          ctx.ca_file = @ca_file if @ca_file
          ctx.cert_file = @cert_file if @cert_file
          ctx.key_file = @key_file if @key_file
          ctx
        end
      end

      def drop
        conn = @conn
        @conn = nil
        conn.close if conn
      end

      def next_packet_id
        @packet_id = (@packet_id + 1) & 0xFFFF
        @packet_id = 1 if @packet_id == 0
        @packet_id
      end

      def send_packet(packet)
        @conn.write_packet(packet)
        @timer.sent
      end

      # Reads one packet. A PUBLISH is kept for receive (and acknowledged
      # at QoS 1), a PINGRESP noted.
      def read_packet
        packet = @conn.read_packet
        if packet.type == PUBLISH
          send_packet(Packet.puback(packet.publish_id)) if packet.qos == 1
          @inbox << [packet.topic, packet.payload]
        elsif packet.type == PINGRESP
          @timer.pingresp_received
        end
        packet
      end

      # Reads until a packet of type comes (with the packet identifier id,
      # unless id is -1) and answers it. Messages that come first wait for
      # receive.
      def wait_for(type, id)
        deadline = MQTT.now + @ack_timeout
        loop do
          left = deadline - MQTT.now
          raise ConnectionError, "no answer from the broker in #{@ack_timeout} s" if left <= 0
          next unless @conn.readable?(left)
          packet = read_packet
          return packet if packet.type == type && (id < 0 || packet.packet_id == id)
        end
      end

      # Sends a ping when one is due; disconnects when the last went unanswered
      def ping_if_due
        if @timer.pingresp_overdue?
          drop
          raise ConnectionError, "no PINGRESP from the broker"
        elsif @timer.ping_due?
          send_packet(Packet.pingreq)
          @timer.ping_sent
        end
      end
    end
  end
end
