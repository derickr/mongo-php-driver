--TEST--
Double type: +1.0
--DESCRIPTION--
Generated by scripts/convert-bson-corpus-tests.php

DO NOT EDIT THIS FILE
--FILE--
<?php

require_once __DIR__ . '/../utils/tools.php';

$bson = hex2bin('10000000016400000000000000F03F00');

// BSON to Canonical BSON
echo bin2hex(fromPHP(toPHP($bson))), "\n";

// BSON to Canonical extJSON
echo json_canonicalize(toExtendedJSON($bson)), "\n";

$json = '{"d" : {"$numberDouble": "1.0"}}';

// extJSON to Canonical extJSON
echo json_canonicalize(toExtendedJSON(fromJSON($json))), "\n";

// extJSON to Canonical BSON
echo bin2hex(fromJSON($json)), "\n";

?>
===DONE===
<?php exit(0); ?>
--EXPECT--
10000000016400000000000000f03f00
{"d":{"$numberDouble":"1.0"}}
{"d":{"$numberDouble":"1.0"}}
10000000016400000000000000f03f00
===DONE===