<?php

require_once __DIR__ . '/db.php';

$db = new Database();
$db->connect();
query('select 1');
