# The byte stream to the broker: a TCPSocket, with an SSLSocket over it
# when an SSLContext is given. Every use of SSLSocket is here, under
# defined?(SSLSocket), so that a program without ssl_socket compiles none
# of it. Once the broker closes the stream, the connection is closed and
# read raises ConnectionError.
module Net
  module MQTT
    class Connection
      def initialize(host, port, ssl_context = nil)
        @tcp = TCPSocket.new(host, port)
        @tls = nil
        @closed = false
        return if ssl_context.nil?
        if defined?(SSLSocket)
          ok = false
          begin
            tls = SSLSocket.new(@tcp, ssl_context)
            tls.hostname = host
            tls.connect
            @tls = tls
            ok = true
          ensure
            close unless ok
          end
        end
      end

      def closed? = @closed

      def write(bytes)
        if defined?(SSLSocket) && tls?
          @tls.write(bytes)
        else
          @tcp.write(bytes)
        end
      end

      def write_packet(packet) = write(packet.encode)
      def read_packet = Packet.read(self)

      # exactly n bytes
      def read(n)
        data = nil
        if defined?(SSLSocket) && tls?
          data = @tls.read(n)
        else
          data = @tcp.read(n)
        end
        if data.nil? || data.bytesize < n
          close
          raise ConnectionError, "connection closed by the broker"
        end
        data
      end

      # whether bytes can be read within seconds (nil: block)
      def readable?(seconds)
        if defined?(SSLSocket)
          return true if tls? && @tls.ready?
        end
        !IO.select([@tcp], nil, nil, seconds).nil?
      end

      def close
        return if @closed
        @closed = true
        if defined?(SSLSocket)
          if tls?
            @tls.close  # closes the TCP socket too (sync_close)
            return
          end
        end
        @tcp.close unless @tcp.closed?
      end

      private

      def tls?
        return false unless defined?(SSLSocket)
        !@tls.nil?
      end
    end
  end
end
