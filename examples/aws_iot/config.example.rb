# Copy this file to config.rb (git ignores it) and fill it in.
WIFI_SSID = "my-ssid"
WIFI_PASSWORD = "my-password"

# AWS IoT Core: Settings > Device data endpoint, and a thing with its
# certificate (activated, with a policy attached) and private key
AWS_IOT_ENDPOINT = "xxxxxxxxxxxxxx-ats.iot.ap-northeast-1.amazonaws.com"
THING_NAME = "xiao-esp32s3"

DEVICE_CERT = <<~PEM
  -----BEGIN CERTIFICATE-----
  ...the thing's certificate (xxxx-certificate.pem.crt)...
  -----END CERTIFICATE-----
PEM

PRIVATE_KEY = <<~PEM
  -----BEGIN RSA PRIVATE KEY-----
  ...its private key (xxxx-private.pem.key)...
  -----END RSA PRIVATE KEY-----
PEM
