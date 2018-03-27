--TEST--
MongoDB\Driver\Manager::executeWriteCommand()
--SKIPIF--
<?php require __DIR__ . "/../utils/basic-skipif.inc"; ?>
<?php NEEDS('STANDALONE'); ?>
<?php skip_if_not_clean(); ?>
<?php skip_if_server_version('<', '3.6'); ?>
--FILE--
<?php
require_once __DIR__ . "/../utils/basic.inc";
require_once __DIR__ . "/../utils/observer.php";

$manager = new MongoDB\Driver\Manager(STANDALONE);

$bw = new MongoDB\Driver\BulkWrite();
$bw->insert(['a' => 1]);
$manager->executeBulkWrite(NS, $bw);

(new CommandObserver)->observe(
    function() use ($manager) {
        $command = new MongoDB\Driver\Command([
            'drop' => COLLECTION_NAME,
        ]);
        $manager->executeWriteCommand(
            DATABASE_NAME,
            $command,
            [
                'writeConcern' => new \MongoDB\Driver\WriteConcern(\MongoDB\Driver\WriteConcern::MAJORITY),
            ]
        );
    },
    function(stdClass $command) {
        echo "Write Concern: ", $command->writeConcern->w, "\n";
    }
);

?>
===DONE===
<?php exit(0); ?>
--EXPECTF--
Write Concern: majority
===DONE===
