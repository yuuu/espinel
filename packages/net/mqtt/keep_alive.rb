# When to ping. The broker drops a client that sends nothing for 1.5 times
# its keep-alive, so a PINGREQ goes out once three quarters of it have
# passed since the last packet sent, and its PINGRESP is awaited for one
# keep-alive. Holds times only; Client sends and reads.
module Net
  module MQTT
    def self.now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    class KeepAlive
      # seconds: 0 turns keep-alive off
      def initialize(seconds)
        @seconds = seconds
        @last_sent = MQTT.now
        @pingresp_due = nil
      end

      def sent
        @last_sent = MQTT.now
      end

      def ping_sent
        @pingresp_due = MQTT.now + @seconds
      end

      def pingresp_received
        @pingresp_due = nil
      end

      def ping_due?
        @seconds > 0 && @pingresp_due.nil? && MQTT.now >= @last_sent + @seconds * 0.75
      end

      def pingresp_overdue?
        due = @pingresp_due
        !due.nil? && MQTT.now >= due
      end

      # seconds until a ping is due or its PINGRESP overdue; nil when off
      def wait
        return nil if @seconds <= 0
        due = @pingresp_due
        t = (due.nil? ? @last_sent + @seconds * 0.75 : due) - MQTT.now
        t < 0 ? 0.0 : t
      end
    end
  end
end
