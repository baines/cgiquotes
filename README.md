This is a CGI program written in C to serve a quote database website.

Currently, it serves data from the `QUOTES_ROOT` directory in 4 formats:
html, json, csv, and "raw" which is just specialized csv with easier parsing
shown here:

```
raw      :: *(raw_line)
raw_line :: id "," epoch "," text "\n"
id       :: *(0-9)
epoch    :: *(0-9)
text     :: *(any char except '\n')
```

Edit quotes.h to change the location of `QUOTES_ROOT`, and add files there
named data-\<name\>. Make sure the name is html-safe.

Later versions will hopefully support PUT / POST / PATCH to actually add/update
quotes. Then it can be used as a replacement for the gist in
[insobot](https://github.com/baines/insobot)'s mod_quotes.

