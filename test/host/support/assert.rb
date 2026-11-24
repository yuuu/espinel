# A minimal assertion helper for the host tests, in the Ruby Spinel
# compiles. A test program calls test(name) { ... } for each case and
# finish at the end, which prints a summary and exits 1 when anything
# failed (an exception escaping a case counts as a failure).

$test_name = ""
$test_assertions = 0
$test_failures = 0

def test_failed(message)
  $test_failures += 1
  puts "FAIL #{$test_name}: #{message}"
end

def test(name)
  $test_name = name
  begin
    yield
  rescue => e
    test_failed("#{e.class}: #{e.message}")
  end
end

def assert(condition, message = "expected a true value")
  $test_assertions += 1
  test_failed(message) unless condition
end

def assert_equal(expected, actual, message = "")
  $test_assertions += 1
  return if expected == actual
  test_failed("#{message.empty? ? "" : "#{message}: "}expected #{expected.inspect}, got #{actual.inspect}")
end

# The block must raise an exception of klass (or a subclass); the message
# is answered so a case can check it.
def assert_raises(klass)
  $test_assertions += 1
  begin
    yield
  rescue => e
    return e.message if e.is_a?(klass)
    test_failed("expected #{klass}, got #{e.class}: #{e.message}")
    return ""
  end
  test_failed("expected #{klass}, nothing was raised")
  ""
end

def finish
  if $test_failures == 0
    puts "ok: #{$test_assertions} assertions"
  else
    puts "#{$test_failures} of #{$test_assertions} assertions failed"
    exit 1
  end
end
