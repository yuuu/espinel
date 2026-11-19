# Mbed TLS, with PicoRuby's interface (picoruby-mbedtls): MbedTLS::Digest,
# HMAC, CMAC, Cipher and PKey::RSA / PKey::EC, on ESP-IDF's SHA and AES
# peripherals and Mbed TLS's keys. The C half is mbedtls.c in this package.
#
# The streaming objects keep their C context in an IO::Buffer, so the GC
# frees it with them; a key is kept as its PEM. `free` is there for
# PicoRuby's sake and does nothing, as in PicoRuby.
module ESPMbedTLS
  ffi_func :espinel_tls_out, [], :binstr
  ffi_func :espinel_tls_error, [:int], :str

  ffi_func :espinel_tls_md_sha1, [], :int
  ffi_func :espinel_tls_md_sha256, [], :int
  ffi_func :espinel_tls_digest_size, [], :int
  ffi_func :espinel_tls_digest_start, [:ptr, :int], :int
  ffi_func :espinel_tls_digest_update, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_digest_finish, [:ptr], :int

  ffi_func :espinel_tls_hmac_size, [], :int
  ffi_func :espinel_tls_hmac_start, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_hmac_reset, [:ptr], :int
  ffi_func :espinel_tls_hmac_update, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_hmac_finish, [:ptr], :int

  ffi_func :espinel_tls_cmac_size, [], :int
  ffi_func :espinel_tls_cmac_start, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cmac_reset, [:ptr], :int
  ffi_func :espinel_tls_cmac_update, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cmac_finish, [:ptr], :int

  ffi_func :espinel_tls_mode_cbc, [], :int
  ffi_func :espinel_tls_mode_gcm, [], :int
  ffi_func :espinel_tls_cipher_size, [], :int
  ffi_func :espinel_tls_cipher_start, [:ptr, :int, :int], :int
  ffi_func :espinel_tls_cipher_set_op, [:ptr, :int], :int
  ffi_func :espinel_tls_cipher_set_key, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cipher_set_iv, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cipher_update_ad, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cipher_update, [:ptr, :str, :size_t], :int
  ffi_func :espinel_tls_cipher_finish, [:ptr], :int
  ffi_func :espinel_tls_cipher_tag, [:ptr], :int
  ffi_func :espinel_tls_cipher_check_tag, [:ptr, :str, :size_t], :int

  ffi_func :espinel_tls_pk_rsa, [], :int
  ffi_func :espinel_tls_pk_ec, [], :int
  ffi_func :espinel_tls_pk_type, [], :int
  ffi_func :espinel_tls_pk_private, [], :int
  ffi_func :espinel_tls_pk_parse, [:str, :size_t, :int], :int
  ffi_func :espinel_tls_pk_public, [:str, :size_t], :int
  ffi_func :espinel_tls_pk_generate_rsa, [:int, :int], :int
  ffi_func :espinel_tls_pk_generate_ec, [:str], :int
  ffi_func :espinel_tls_pk_sign, [:str, :size_t, :int, :str, :size_t], :int
  ffi_func :espinel_tls_pk_verify, [:str, :size_t, :int, :str, :size_t, :str, :size_t], :int

  # rc when it is not an error; raises klass otherwise
  def self.check(rc, what, klass = RuntimeError)
    raise klass, "#{what}: #{ESPMbedTLS.espinel_tls_error(rc)}" if rc < 0
    rc
  end

  # the bytes a call left, once check has passed it
  def self.out(rc, what, klass = RuntimeError)
    ESPMbedTLS.check(rc, what, klass)
    ESPMbedTLS.espinel_tls_out
  end
end

module MbedTLS
  class Digest
    SUPPORTED_ALGORITHMS = {
      sha1: ESPMbedTLS.espinel_tls_md_sha1,
      sha256: ESPMbedTLS.espinel_tls_md_sha256
    }

    attr_reader :algorithm

    def initialize(algorithm)
      @algorithm = SUPPORTED_ALGORITHMS[algorithm] || 0
      raise ArgumentError, "invalid algorithm" if @algorithm == 0
      @ctx = IO::Buffer.new(ESPMbedTLS.espinel_tls_digest_size)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_digest_start(@ctx, @algorithm), "Digest")
    end

    def update(input)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_digest_update(@ctx, input, input.bytesize), "Digest#update")
      self
    end

    def finish
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_digest_finish(@ctx), "Digest#finish")
    end

    def free = nil
  end

  class HMAC
    def initialize(key, digest)
      raise ArgumentError, "Unsupported digest: #{digest}" unless digest.downcase == "sha256"
      @ctx = IO::Buffer.new(ESPMbedTLS.espinel_tls_hmac_size)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_hmac_start(@ctx, key, key.bytesize), "HMAC", ArgumentError)
    end

    def update(input)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_hmac_update(@ctx, input, input.bytesize), "HMAC#update")
      self
    end

    def reset
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_hmac_reset(@ctx), "HMAC#reset")
      self
    end

    def digest
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_hmac_finish(@ctx), "HMAC#digest")
    end

    def hexdigest
      digest.unpack1("H*")
    end

    def free = nil
  end

  class CMAC
    def initialize(key, digest)
      raise ArgumentError, "Unsupported digest: #{digest}" unless digest.downcase == "aes"
      raise ArgumentError, "Invalid key length: `#{key.inspect}`" unless key.bytesize == 16
      @ctx = IO::Buffer.new(ESPMbedTLS.espinel_tls_cmac_size)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cmac_start(@ctx, key, key.bytesize), "CMAC", ArgumentError)
    end

    def update(input)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cmac_update(@ctx, input, input.bytesize), "CMAC#update")
      self
    end

    def reset
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cmac_reset(@ctx), "CMAC#reset")
      self
    end

    def digest
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_cmac_finish(@ctx), "CMAC#digest")
    end

    def free = nil
  end

  class Cipher
    CBC = ESPMbedTLS.espinel_tls_mode_cbc
    GCM = ESPMbedTLS.espinel_tls_mode_gcm
    # name => [mode, key length]
    SUITES = {
      "AES-128-CBC" => [CBC, 16],
      "AES-192-CBC" => [CBC, 24],
      "AES-256-CBC" => [CBC, 32],
      "AES-128-GCM" => [GCM, 16],
      "AES-192-GCM" => [GCM, 24],
      "AES-256-GCM" => [GCM, 32]
    }

    def self.ciphers
      SUITES.keys
    end

    attr_reader :key_len, :iv_len

    def initialize(cipher_suite)
      suite = SUITES[cipher_suite]
      raise ArgumentError, "unsupported cipher suite: #{cipher_suite}" if suite.nil?
      @mode = suite[0]
      @key_len = suite[1]
      @iv_len = @mode == GCM ? 12 : 16
      @ctx = IO::Buffer.new(ESPMbedTLS.espinel_tls_cipher_size)
      ESPMbedTLS.espinel_tls_cipher_start(@ctx, @mode, @key_len)
    end

    def encrypt
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_set_op(@ctx, 1), "Cipher#encrypt")
      self
    end

    def decrypt
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_set_op(@ctx, 0), "Cipher#decrypt")
      self
    end

    def key=(key)
      raise ArgumentError, "key length is invalid" if key.bytesize != @key_len
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_set_key(@ctx, key, key.bytesize), "Cipher#key=")
      key
    end

    def iv=(iv)
      raise ArgumentError, "iv length is invalid" if iv.bytesize != @iv_len
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_set_iv(@ctx, iv, iv.bytesize), "Cipher#iv=")
      iv
    end

    def update_ad(input)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_update_ad(@ctx, input, input.bytesize), "Cipher#update_ad")
      self
    end

    def update(input)
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_cipher_update(@ctx, input, input.bytesize), "Cipher#update")
    end

    def finish
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_cipher_finish(@ctx), "Cipher#finish")
    end

    def write_tag
      ESPMbedTLS.out(ESPMbedTLS.espinel_tls_cipher_tag(@ctx), "Cipher#write_tag")
    end

    def check_tag(tag)
      ESPMbedTLS.check(ESPMbedTLS.espinel_tls_cipher_check_tag(@ctx, tag, tag.bytesize), "Cipher#check_tag") == 1
    end

    def free = nil
  end

  module PKey
    class PKeyError < StandardError
    end

    RSA_TYPE = ESPMbedTLS.espinel_tls_pk_rsa
    EC_TYPE = ESPMbedTLS.espinel_tls_pk_ec

    class PKeyBase
      def sign(digest, input)
        rc = ESPMbedTLS.espinel_tls_pk_sign(@pem, @pem.bytesize, digest.algorithm, input, input.bytesize)
        ESPMbedTLS.out(rc, "PKey error", PKeyError)
      end

      def verify(digest, signature, input)
        rc = ESPMbedTLS.espinel_tls_pk_verify(@pem, @pem.bytesize, digest.algorithm,
                                              signature, signature.bytesize, input, input.bytesize)
        ESPMbedTLS.check(rc, "PKey error", PKeyError) == 1
      end

      # the key's PEM as Mbed TLS writes it: the private key when it has one
      def to_pem = @pem
      def to_s = @pem
      def export = @pem
      def public? = true
      def private? = @private_key

      def free = nil

      private

      # reads key (PEM or DER) of type
      def load(key, type)
        @pem = ESPMbedTLS.out(ESPMbedTLS.espinel_tls_pk_parse(key, key.bytesize, type), "PKey error", PKeyError)
        @private_key = ESPMbedTLS.espinel_tls_pk_private != 0
      end

      def public_pem
        ESPMbedTLS.out(ESPMbedTLS.espinel_tls_pk_public(@pem, @pem.bytesize), "PKey error", PKeyError)
      end
    end

    class RSA < PKeyBase
      # RSA.new(pem) reads a key; RSA.new(bits, exponent = 65537) makes one
      def initialize(key, exponent = 65537)
        if key.is_a?(Integer)
          rc = ESPMbedTLS.espinel_tls_pk_generate_rsa(key, exponent)
          @pem = ESPMbedTLS.out(rc, "PKey error", PKeyError)
          @private_key = true
        else
          load(key, RSA_TYPE)
        end
      end

      def self.generate(bits, exponent = 65537)
        new(bits, exponent)
      end

      def public_key = RSA.new(public_pem)
    end

    class EC < PKeyBase
      def initialize(key)
        load(key, EC_TYPE)
      end

      def self.generate(curve = "secp256r1")
        new(ESPMbedTLS.out(ESPMbedTLS.espinel_tls_pk_generate_ec(curve), "PKey error", PKeyError))
      end

      def public_key = EC.new(public_pem)
    end
  end
end
