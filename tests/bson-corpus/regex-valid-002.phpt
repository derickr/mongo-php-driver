--TEST--
Regular Expression type: regex without options
--DESCRIPTION--
Generated by scripts/convert-bson-corpus-tests.php

DO NOT EDIT THIS FILE
--FILE--
<?php

require_once __DIR__ . '/../utils/tools.php';

$bson = hex2bin('0D0000000B6100616263000000');

// BSON to Canonical BSON
echo bin2hex(fromPHP(toPHP($bson))), "\n";

// BSON to Canonical extJSON
echo json_canonicalize(toExtendedJSON($bson)), "\n";

$json = '{"a" : {"$regex" : "abc", "$options" : ""}}';

// extJSON to Canonical extJSON
echo json_canonicalize(toExtendedJSON(fromJSON($json))), "\n";

// extJSON to Canonical BSON
echo bin2hex(fromJSON($json)), "\n";

?>
===DONE===
<?php exit(0); ?>
--EXPECT--
0d0000000b6100616263000000
{"a":{"$regex":"abc","$options":""}}
{"a":{"$regex":"abc","$options":""}}
0d0000000b6100616263000000
===DONE===