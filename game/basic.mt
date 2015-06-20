extern joinchars
extern printf
extern floor
extern halt
extern tostring
extern erase
extern type
extern strcat
extern fopen
extern getc
extern putc
extern bytes
extern getbyte
extern setbyte
extern setint
extern lenbytes
extern assert
extern sin
extern cos
extern atan2
extern sqrt
extern rand
extern srand

var eof
var on_exit

var number_type
var string_type
var array_type
var func_type
var dict_type
var native_type

func mprintf(format, ...)
	return mprintf_args(format, args)
end

func _mprintf_add_chars(buf, string)
	for var i = 0, i < len(string), i += 1
		push(buf, string[i])
	end
end

func _mprintf_tostring(v)
	var t = type(v)
	if t == "string"
		return v
	elif t == "dict"
		var buf = []
		var p = v.pairs
		
		push(buf, '{')
		push(buf, ' ')
		
		for var i = 0, i < len(p), i += 1
			_mprintf_add_chars(buf, p[i][0])
			push(buf, ' ')
			push(buf, '=')
			push(buf, ' ')
			_mprintf_add_chars(buf, _mprintf_tostring(p[i][1]))
			
			if i + 1 < len(p)
				push(buf, ',')
				push(buf, ' ')
			end
		end
		
		push(buf, ' ')
		push(buf, '}')
		
		return joinchars(buf)
	elif t == "array"
		var buf = []
		push(buf, '[')
		for var i = 0, i < len(v), i += 1
			_mprintf_add_chars(buf, _mprintf_tostring(v[i]))
			if i + 1 < len(v)
				push(buf, ',')
			end
		end
		push(buf, ']')
		
		return joinchars(buf)
	else 
		return tostring(v)
	end
end

func mprintf_raw(format, ...)
	var buf = []
	var arg = 0
	
	for var i = 0, i < len(format), i += 1
		if format[i] == '%'
			i += 1
			if arg >= len(args)
				printf("mprintf format overflow\n")
				return buf
			end
			
			if format[i] == 'c'
				push(buf, args[arg])
			elif format[i] == 'g'
				_mprintf_add_chars(buf, tostring(args[arg]))
			elif format[i] == 's'
				_mprintf_add_chars(buf, args[arg])
			elif format[i] == 'o'
				_mprintf_add_chars(buf, _mprintf_tostring(args[arg]))
			else
				printf("invalid mprintf format specifier '%c'\n", format[i])
				return joinchars(buf)
			end
			arg += 1
		else
			push(buf, format[i])
		end
	end
	
	return buf
end

func mprintf_args(format, args)
	var buf = []
	var arg = 0
	
	for var i = 0, i < len(format), i += 1
		if format[i] == '%'
			i += 1
			if arg >= len(args)
				printf("mprintf format overflow\n")
				return joinchars(buf)
			end
			
			if format[i] == 'c'
				push(buf, args[arg])
			elif format[i] == 'g'
				_mprintf_add_chars(buf, tostring(args[arg]))
			elif format[i] == 's'
				_mprintf_add_chars(buf, args[arg])
			elif format[i] == 'o'
				_mprintf_add_chars(buf, _mprintf_tostring(args[arg]))
			else
				printf("invalid mprintf format specifier '%c'\n", format[i])
				return joinchars(buf)
			end
			arg += 1
		else
			push(buf, format[i])
		end
	end
	
	return joinchars(buf)
end

func number_cmp(x, y)
	return x - y
end

func string_cmp(x, y)
	if len(x) < len(y)
		for var i = 0, i < len(x), i += 1
			if x[i] != y[i]
				return x[i] - y[i]
			end
		end
	else
		for var i = 0, i < len(x), i += 1
			if x[i] != y[i]
				return x[i] - y[i]
			end
		end
	end
	
	return 0
end

func sort(v, comp)
	for var i = 0, i < len(v), i += 1
		for var j = 0, j < len(v) - 1, j += 1
			var c = comp(v[j], v[j + 1])
			
			if c > 0
				var temp = v[j + 1]
				v[j + 1] = v[j]
				v[j] = temp
			end
		end
	end
end

func isalpha(c)
	if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') return true end
	return false
end

func isdigit(c)
	if (c >= '0' && c <= '9') return true end
	return false
end

func isalnum(c)
	if isalpha(c) || isdigit(c) return true end
	return false
end

func isspace(c)
	if c == '\n' || c == '\t' || c == ' ' return true end
	return false
end

func stream(...)
	var raw = ""
	if len(args) > 0
		raw = args[0]
	end

	return { _raw = raw, _ext = [], pos = 0, read = stream_read, write = stream_write 
			 write_u8 = stream_write_u8, write_u16 = stream_write_u16, write_u32 = stream_write_u32, 
			 write_bytes = stream_write_bytes, close = stream_close, _file_handle = null, _mode = null }
end

func stream_is_eof(self)
	_stream_ext(self)
	return (len(self._raw) - self.pos) == 0
end

func _stream_ext(self)
	if len(self._ext) > 0
		self._raw = strcat(self._raw, joinchars(self._ext))
		clear(self._ext)
	end
end

func stream_read(self)
	_stream_ext(self)
	if self.pos >= len(self._raw)
		return eof
	end
	
	var c = self._raw[self.pos]
	self.pos += 1
	
	return c
end

func stream_write(self, c)
	if self.pos >= len(self._raw)
		if type(c) == number_type
			push(self._ext, c)
		elif type(c) == string_type || type(c) == array_type
			for var i = 0, i < len(c), i += 1
				stream_write(self, c[i])
			end
		end
	else
		if type(c) == number_type
			self._raw[self.pos] = c
			self.pos += 1
		elif type(c) == string_type || type(c) == array_type
			for var i = 0, i < len(c), i += 1
				stream_write(self, c[i])
			end
		end
	end
end

func stream_write_u8(self, value)
	stream_write(self, floor(value % 256))
end

func stream_write_u16(self, value)
	stream_write(self, floor(value % 65535))
end

func stream_write_u32(self, value)
	stream_write(self, floor(value % 4294967295))
end

func stream_write_bytes(self, array)
	for var i = 0, i < len(array), i += 1
		stream_write(self, floor(array[i] % 256))
	end
end

func stream_close(self)
	if self._file_handle != null && self._mode[0] == 'w'
		_stream_ext(self)
		for var i = 0, i < len(self._raw), i += 1
			putc(self._file_handle, self._raw[i])
		end
	end
end

func fstream(path, mode)
	var f = fopen(path, mode)
	var buf = []
	var c = getc(f)
	
	while c != eof
		push(buf, c)
		c = getc(f)
	end
	
	var s = stream(joinchars(buf))
	s._file_handle = f
	s._mode = mode
	return s
end

func at_exit(f, ...)
	push(on_exit, [f, args])
end

func exit()
	for var i = 0, i < len(on_exit), i += 1
		var f = on_exit[i][0]
		var args = on_exit[i][1]
		
		f(args)
	end
	
	1
	halt()
end

func _main()
	eof = -1
	on_exit = []

	number_type = type(0)
	string_type = type("")
	array_type = type([])
	func_type = type(_main)
	dict_type = type({})
	native_type = "native"
	
	var r = main()
	
	for var i = 0, i < len(on_exit), i += 1
		var f = on_exit[i][0]
		var args = on_exit[i][1]
		
		f(args)
	end
	
	return r
end