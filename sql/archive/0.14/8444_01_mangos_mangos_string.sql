ALTER TABLE db_version CHANGE COLUMN required_8416_01_mangos_spell_learn_spell required_8444_01_mangos_mangos_string bit;

DELETE FROM mangos_string WHERE entry IN(348,522);
INSERT INTO mangos_string VALUES
(348,'Game Object (Entry: %u) have invalid data and can\'t be spawned',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
(522,'Game Object (Entry: %u) not found',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
