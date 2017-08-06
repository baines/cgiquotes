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
named \#name. Make sure the name is html-safe and the dir has rw permissions.

It also now supports POST and DELETE to add/edit/delete quotes remotely.
For this to work, you need to choose a random username + password, concat them
with a colon, base64 it, and put it in QUOTES_AUTH env var. Then you can use the
url 'https://USERNAME:PASSWORD@asdf.test/quotes/' for POST/DELETE operations.

POSTing data to /quotes/name will add that data as a new quote, and return the
id + timestamp of this new quote, separated by a comma.

POSTing data of the form '\[epoch\]:\[text\]' to /quotes/name/id will edit the
quote's epoch or text (or both). If you only want to change the text, remember
to include that leading colon.

DELETEing the url /quotes/name/id will.. well, it'll delete the quote, 
what did you expect?

Example nginx configutation (using fcgiwrap):

```
location /quotes {
	root /var/www/quotes/;

	fastcgi_param SCRIPT_FILENAME /var/www/quotes/cgi-bin/quotes;
	fastcgi_param DOCUMENT_URI    $document_uri;
	fastcgi_param REQUEST_URI     $request_uri;
	fastcgi_param REQUEST_METHOD  $request_method;
	fastcgi_param CONTENT_LENGTH  $content_length;

	fastcgi_param QUOTES_AUTH     [base64'd user:pwd]

	fastcgi_pass unix:/var/run/fcgiwrap.socket;
}
```

