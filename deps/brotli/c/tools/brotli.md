# NAME

brotli(1) -- brotli, unbrotli - compress or decompress files

# SYNOPSIS

`brotli` [*OPTION|FILE*]...

`unbrotli` is equivalent to `brotli --decompress`

# DESCRIPTION

`brotli` is a generic-purpose lossless compression algorithm that compresses
data using a combination of a modern variant of the **LZ77** algorithm, Huffman
coding and 2-nd order context modeling, with a compression ratio comparable to
the best currently available general-purpose compression methods. It is similar
in speed with deflate but offers more dense compression.

`brotli` command line syntax similar to `gzip (1)` and `zstd (1)`.
Unlike `gzip (1)`, source files are preserved by default. It is possible to
remove them after processing by using the `--rm` _option_.

Arguments that look like "`--name`" or "`--name=value`" are _options_. Every
_option_ has a short form "`-x`" or "`-x value`". Multiple short form _options_
could be coalesced:

* "`--decompress --stdout --suffix=.b`" works the same as
* "`-d -s -S .b`" and
* "`-dsS .b`"

`brotli` has 3 operation modes:

* default mode is compression;
* `--decompress` option activates decompression mode;
* `--test` option switches to integrity test mode; this option is equivalent to
  "`--decompress --stdout`" except that the decompressed data is discarded
  instead of being written to standard output.

Every non-option argument is a _file_ entry. If no _files_ are given or _file_
is "`-`", `brotli` reads from standard input. All arguments after "`--`" are
_file_ entries.

Unless `--stdout` or `--output` is specified, _files_ are written to a new file
whose name is derived from the source _file_ name:

* when compressing, a suffix is appended to the source filename to
  get the target filename
* when decompressing, a suffix is removed from the source filename to
  get the target filename

Default suffix is `.br`, but it could be specified with `--suffix` option.

Conflicting or duplicate _options_ are not allowed.

# OPTIONS

* `-#`:
    compression level (0-9); bigger values cause denser, but slower compression
* `-c`, `--stdout`:
    write on standard output
* `-d`, `--decompress`:
    decompress mode
* `-f`, `--force`:
    force output file overwrite
* `-h`, `--help`:
    display this help and exit
* `-j`, `--rm`:
    remove source file(s); `gzip (1)`-like behaviour
* `-k`, `--keep`:
    keep source file(s); `zstd (1)`-like behaviour
* `-n`, `--no-copy-stat`:
    do not copy source file(s) attributes
* `-o FILE`, `--output=FILE`
    output file; valid only if there is a single input entry
* `-q NUM`, `--quality=NUM`:
    compression level (0-11); bigger values cause denser, but slower compression
* `-t`, `--test`:
    test file integrity mode
* `-v`, `--verbose`:
    increase output verbosity
* `-w NUM`, `--lgwin=NUM`:
    set LZ77 window size (0, 10-24) (default: 24); window size is
    `(pow(2, NUM) - 16)`; 0 lets compressor decide over the optimal value;
    bigger windows size improve density; decoder might require up to window size
    memory to operate
* `-D FILE`, `--dictionary=FILE`:
    use FILE as raw (LZ77) dictionary; same dictionary MUST be used both for
    compression and decompression
* `-S SUF`, `--suffix=SUF`:
    output file suffix (default: `.br`)
* `-V`, `--version`:
    display version and exit
* `-Z`, `--best`:
    use best compression level (default); same as "`-q 11`"

# SEE ALSO

`brotli` file format is defined in
[RFC 7932](https://www.ietf.org/rfc/rfc7932.txt).

`brotli` is open-sourced under the
[MIT License](https://opensource.org/licenses/MIT).

Mailing list: https://groups.google.com/forum/#!forum/brotli

# BUGS

Report bugs at: https://github.com/google/brotli/issues
