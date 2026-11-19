# TLS clients, with PicoRuby's interface (picoruby-socket's SSLContext and
# SSLSocket) and the parts of CRuby's OpenSSL::SSL::SSLSocket that fit
# (hostname=, sync_close, sysread/syswrite, to_io), over Spinel's
# TCPSocket and ESP-IDF's Mbed TLS. The C half is ssl_socket.c in this
# package.
#
#   ssl = SSLSocket.open("example.com", 443, SSLContext.new)
#   ssl.write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
#   puts ssl.gets
#   ssl.close
#
# With VERIFY_PEER (the default) and no CA given, the server is checked
# against ESP-IDF's certificate bundle. Close a socket when done: its
# session holds about 20 KB, which the GC does not free.
require "socket"

module ESPSSL
  ffi_func :espinel_ssl_message, [], :str
  ffi_func :espinel_ssl_timed_out, [:int], :int
  ffi_func :espinel_ssl_connect, [:int, :str, :int, :str, :size_t, :str, :size_t, :str, :size_t, :int], :int
  ffi_func :espinel_ssl_write, [:int, :str, :size_t, :int], :int
  ffi_func :espinel_ssl_read, [:int, :size_t, :int], :int
  ffi_func :espinel_ssl_out, [], :binstr
  ffi_func :espinel_ssl_pending, [:int], :int
  ffi_func :espinel_ssl_verify_result, [:int], :int
  ffi_func :espinel_ssl_version, [:int], :str
  ffi_func :espinel_ssl_cipher, [:int], :str
  ffi_func :espinel_ssl_close, [:int], :int

  def self.check(rc)
    return rc if rc >= 0
    raise IO::TimeoutError, ESPSSL.espinel_ssl_message if ESPSSL.espinel_ssl_timed_out(rc) != 0
    raise SocketError, ESPSSL.espinel_ssl_message
  end
end

class SSLContext
  VERIFY_NONE = 0
  VERIFY_PEER = 1

  attr_reader :verify_mode, :ca_pem, :cert_pem, :key_pem

  def initialize
    @verify_mode = VERIFY_PEER
    @ca_pem = ""
    @cert_pem = ""
    @key_pem = ""
  end

  def verify_mode=(mode)
    raise ArgumentError, "invalid verify mode: #{mode}" unless mode == VERIFY_NONE || mode == VERIFY_PEER
    @verify_mode = mode
  end

  # the CA certificates (PEM, one or more) to check the server against, in
  # place of the bundle
  def set_ca_pem(pem)
    @ca_pem = pem
  end

  # a client certificate and its key (PEM)
  def set_cert_pem(pem)
    @cert_pem = pem
  end

  def set_key_pem(pem)
    @key_pem = pem
  end

  def ca_file=(path)
    set_ca_pem(File.read(path))
  end

  def cert_file=(path)
    set_cert_pem(File.read(path))
  end

  def key_file=(path)
    set_key_pem(File.read(path))
  end
end

class SSLSocket
  attr_reader :tcp_socket, :ssl_context, :hostname
  # close closes the TCP socket too (PicoRuby's behavior; CRuby's default is false)
  attr_accessor :sync_close
  # seconds a handshake, read or write may wait (nil: no limit); IO::TimeoutError after
  attr_accessor :timeout

  def self.open(host, port, ssl_context = SSLContext.new)
    ssl = SSLSocket.new(TCPSocket.new(host, port), ssl_context)
    ssl.hostname = host
    ssl.connect
  end

  def initialize(tcp_socket, ssl_context = SSLContext.new)
    @tcp_socket = tcp_socket
    @ssl_context = ssl_context
    @hostname = nil
    @sync_close = true
    @timeout = nil
    @handle = -1
    @rbuf = "".b
    @eof = false
  end

  # the server's name, for SNI and the certificate check
  def hostname=(name)
    @hostname = name
  end

  def connect
    ctx = @ssl_context
    host = @hostname
    if host.nil? && ctx.verify_mode == SSLContext::VERIFY_PEER
      raise SocketError, "SSLSocket#hostname= is needed to verify the server (or use SSLSocket.open)"
    end
    @handle = ESPSSL.check(ESPSSL.espinel_ssl_connect(@tcp_socket.fileno, host, ctx.verify_mode,
                                                      ctx.ca_pem, ctx.ca_pem.bytesize,
                                                      ctx.cert_pem, ctx.cert_pem.bytesize,
                                                      ctx.key_pem, ctx.key_pem.bytesize, timeout_ms))
    self
  end

  def connected? = @handle >= 0
  def closed? = @handle < 0
  def ready? = @rbuf.bytesize > 0 || (connected? && ESPSSL.espinel_ssl_pending(@handle) > 0)

  def syswrite(data)
    s = data.to_s
    ESPSSL.check(ESPSSL.espinel_ssl_write(handle, s, s.bytesize, timeout_ms))
  end

  def write(*strs)
    total = 0
    strs.each { |s| total += syswrite(s) }
    total
  end

  def <<(data)
    syswrite(data)
    self
  end

  def print(*args)
    args.each { |a| syswrite(a.to_s) }
    nil
  end

  def puts(*args)
    if args.empty?
      syswrite("\n")
    else
      args.each do |a|
        s = a.to_s
        syswrite(s.end_with?("\n") ? s : s + "\n")
      end
    end
    nil
  end

  # up to maxlen bytes, what is there once something is; EOFError at the end
  def readpartial(maxlen)
    if @rbuf.bytesize > 0
      out = @rbuf.byteslice(0, maxlen)
      @rbuf = @rbuf.byteslice(out.bytesize, @rbuf.bytesize - out.bytesize)
      return out
    end
    chunk = fill(maxlen)
    raise EOFError, "end of file reached" if chunk.nil?
    chunk
  end

  def sysread(maxlen) = readpartial(maxlen)

  # read: everything to the end; read(n): n bytes, fewer at the end, nil
  # when nothing is left
  def read(maxlen = nil)
    if maxlen.nil?
      chunks = [@rbuf]
      @rbuf = "".b
      while (chunk = fill(4096))
        chunks << chunk
      end
      return chunks.join
    end
    return "".b if maxlen == 0
    while @rbuf.bytesize < maxlen
      chunk = fill(maxlen - @rbuf.bytesize)
      break if chunk.nil?
      @rbuf += chunk
    end
    return nil if @rbuf.bytesize == 0
    out = @rbuf.byteslice(0, maxlen)
    @rbuf = @rbuf.byteslice(out.bytesize, @rbuf.bytesize - out.bytesize)
    out
  end

  # a line, its separator kept; nil at the end
  def gets(sep = "\n")
    loop do
      i = @rbuf.index(sep)
      if i
        n = i + sep.bytesize
        line = @rbuf.byteslice(0, n)
        @rbuf = @rbuf.byteslice(n, @rbuf.bytesize - n)
        return line
      end
      chunk = fill(1024)
      if chunk.nil?
        return nil if @rbuf.bytesize == 0
        line = @rbuf
        @rbuf = "".b
        return line
      end
      @rbuf += chunk
    end
  end

  def each_line(sep = "\n")
    while (line = gets(sep))
      yield line
    end
    self
  end

  def eof?
    return false if @rbuf.bytesize > 0
    chunk = fill(1024)
    return true if chunk.nil?
    @rbuf += chunk
    false
  end

  def close
    if @handle >= 0
      ESPSSL.espinel_ssl_close(@handle)
      @handle = -1
    end
    @tcp_socket.close if @sync_close && !@tcp_socket.closed?
    nil
  end

  def to_io = @tcp_socket
  def io = @tcp_socket
  def addr = @tcp_socket.addr
  def peeraddr = @tcp_socket.peeraddr
  def remote_host = @hostname || @tcp_socket.peeraddr[3]
  def remote_port = @tcp_socket.peeraddr[1]

  # the protocol, as Mbed TLS names it ("TLSv1.2")
  def ssl_version = connected? ? ESPSSL.espinel_ssl_version(@handle) : nil

  # [cipher suite, protocol], Mbed TLS's names
  def cipher = connected? ? [ESPSSL.espinel_ssl_cipher(@handle), ssl_version] : nil

  def state = connected? ? "connected" : "closed"

  # Mbed TLS's verification flags for the server's certificate: 0 when it
  # was verified (MBEDTLS_X509_BADCERT_* bits otherwise; with VERIFY_NONE,
  # MBEDTLS_X509_BADCERT_SKIP_VERIFY, 128)
  def verify_result = connected? ? ESPSSL.espinel_ssl_verify_result(@handle) : nil

  private

  def handle
    raise IOError, "closed stream" if @handle < 0
    @handle
  end

  def timeout_ms
    t = @timeout
    t.nil? ? -1 : (t * 1000).to_i
  end

  # the next bytes from the session, up to max; nil at the end
  def fill(max)
    return nil if @eof
    n = ESPSSL.check(ESPSSL.espinel_ssl_read(handle, max, timeout_ms))
    if n == 0
      @eof = true
      return nil
    end
    ESPSSL.espinel_ssl_out
  end
end
