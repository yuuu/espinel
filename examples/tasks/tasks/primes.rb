require "task"

# Counts the primes below a growing limit, as hard as it can, and sends
# each count to main.rb. It shares no object with main.rb: a report is a
# String copied through the queue.
def prime?(n)
  return false if n < 2
  d = 2
  while d * d <= n
    return false if n % d == 0
    d += 1
  end
  true
end

reports = Task::Queue.new(:reports)
reports.push("primes on core #{Task.core}")
count = 0
n = 0
[50_000, 100_000, 200_000, 400_000, 800_000].each do |limit|
  while n < limit
    count += 1 if prime?(n)
    n += 1
  end
  reports.push("#{count} primes below #{limit}")
end
