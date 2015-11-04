struct foo
	x : number
	y : string
	z : foo
end

struct bar
	x : number
end

struct baz
	quack : array
end

write(typemembers(foo))

check_safe_cast(foo, bar)
check_safe_cast(foo, baz)
