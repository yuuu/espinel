# exit runs the at_exit handlers, last registered first, and ends with the
# status it is given (main.c's exit, not ESP-IDF's abort).
at_exit { puts "first registered, runs last" }
at_exit { puts "second registered, runs first" }
puts "exiting"
exit 0
puts "not reached"
