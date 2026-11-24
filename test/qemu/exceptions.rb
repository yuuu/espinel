# raise, rescue, ensure, retry, custom exceptions, catch/throw, and
# SystemStackError from a recursion that would overrun the Ruby task's
# stack (main/stack_check.c).
class AppError < StandardError
  def initialize(msg = "app failed") = super
end

def risky(n)
  raise AppError if n == 0
  raise ArgumentError, "negative: #{n}" if n < 0
  n * 2
end

[1, 0, -1].each do |n|
  begin
    puts "risky(#{n}) = #{risky(n)}"
  rescue AppError => e
    puts "AppError: #{e.message}"
  rescue ArgumentError => e
    puts "ArgumentError: #{e.message}"
  ensure
    puts "ensure #{n}"
  end
end

tries = 0
begin
  tries += 1
  raise "flaky" if tries < 3
  puts "succeeded after #{tries} tries"
rescue
  retry
end

r = catch(:found) do
  [[1, 2], [3, 4]].each { |row| row.each { |x| throw :found, x if x > 2 } }
  nil
end
puts "caught #{r}"

def down(n) = n == 0 ? 0 : 1 + down(n - 1)
puts down(100)

def forever(n) = forever(n + 1) + 1
begin
  forever(0)
rescue SystemStackError => e
  puts "SystemStackError: #{e.class}"
end
puts "still running"
