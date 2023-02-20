SELECT (number * 2)::Int128 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM 3 TO 8;
SELECT (number * 2)::Int256 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM 3 TO 8;
SELECT (number * 2)::UInt128 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM 3 TO 8;
SELECT (number * 2)::UInt256 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM 3 TO 8;

SELECT (number * 2)::Int128 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM -3 TO 5;
SELECT (number * 2)::Int256 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM -3 TO 5;
SELECT (number * 2)::UInt128 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM -3 TO 5; -- { serverError 69 }
SELECT (number * 2)::UInt256 FROM numbers(10) ORDER BY 1 ASC WITH FILL FROM -3 TO 5; -- { serverError 69 }
