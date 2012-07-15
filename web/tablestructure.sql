-- MySQL dump 10.13  Distrib 5.5.24, for debian-linux-gnu (x86_64)
--
-- Host: localhost    Database: cryptoms
-- ------------------------------------------------------
-- Server version	5.5.24-3

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `clauseSizeDistrib`
--

DROP TABLE IF EXISTS `clauseSizeDistrib`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `clauseSizeDistrib` (
  `runID` bigint(20) unsigned NOT NULL,
  `conflicts` bigint(20) unsigned NOT NULL,
  `size` int(10) unsigned NOT NULL,
  `num` bigint(20) unsigned NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `clauseStats`
--

DROP TABLE IF EXISTS `clauseStats`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `clauseStats` (
  `runID` bigint(20) unsigned NOT NULL,
  `simplifications` bigint(20) unsigned NOT NULL,
  `reduceDB` bigint(20) unsigned NOT NULL,
  `learnt` int(10) unsigned NOT NULL,
  `size` bigint(20) unsigned NOT NULL,
  `glue` bigint(20) unsigned NOT NULL,
  `numPropAndConfl` bigint(20) unsigned NOT NULL,
  `numLitVisited` bigint(20) unsigned NOT NULL,
  `numLookedAt` bigint(20) unsigned NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `fileNamesUsed`
--

DROP TABLE IF EXISTS `fileNamesUsed`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `fileNamesUsed` (
  `runID` bigint(20) NOT NULL,
  `filename` varchar(500) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `restart`
--

DROP TABLE IF EXISTS `restart`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `restart` (
  `runID` bigint(20) unsigned NOT NULL,
  `simplifications` bigint(20) unsigned NOT NULL,
  `restarts` bigint(20) unsigned NOT NULL,
  `conflicts` bigint(20) unsigned NOT NULL,
  `time` double unsigned NOT NULL,
  `glue` double unsigned NOT NULL,
  `size` double unsigned NOT NULL,
  `resolutions` double unsigned NOT NULL,
  `branchDepth` double unsigned NOT NULL,
  `branchDepthDelta` double unsigned NOT NULL,
  `trailDepth` double unsigned NOT NULL,
  `trailDepthDelta` double unsigned NOT NULL,
  `agility` double unsigned NOT NULL,
  `glueSD` double unsigned NOT NULL,
  `sizeSD` double unsigned NOT NULL,
  `resolutionsSD` double unsigned NOT NULL,
  `branchDepthSD` double unsigned NOT NULL,
  `branchDepthDeltaSD` double unsigned NOT NULL,
  `trailDepthSD` double unsigned NOT NULL,
  `trailDepthDeltaSD` double unsigned NOT NULL,
  `agilitySD` double unsigned NOT NULL,
  `propBinIrred` bigint(20) unsigned NOT NULL,
  `propBinRed` bigint(20) unsigned NOT NULL,
  `propTriIrred` bigint(20) unsigned NOT NULL,
  `propTriRed` bigint(20) unsigned NOT NULL,
  `propLongIrred` bigint(20) unsigned NOT NULL,
  `propLongRed` bigint(20) unsigned NOT NULL,
  `conflBinIrred` bigint(20) unsigned NOT NULL,
  `conflBinRed` bigint(20) unsigned NOT NULL,
  `conflTriIrred` bigint(20) unsigned NOT NULL,
  `conflTriRed` bigint(20) unsigned NOT NULL,
  `conflLongIrred` bigint(20) unsigned NOT NULL,
  `conflLongRed` bigint(20) unsigned NOT NULL,
  `learntUnits` bigint(20) unsigned NOT NULL,
  `learntBins` bigint(20) unsigned NOT NULL,
  `learntTris` bigint(20) unsigned NOT NULL,
  `learntLongs` bigint(20) unsigned NOT NULL,
  `conflAfterConfl` double unsigned NOT NULL,
  `conflAfterConflSD` double unsigned NOT NULL,
  `watchListSizeTraversed` double unsigned NOT NULL,
  `watchListSizeTraversedSD` double unsigned NOT NULL,
  `litPropagatedSomething` double unsigned NOT NULL,
  `litPropagatedSomethingSD` double unsigned NOT NULL,
  `propagations` bigint(20) unsigned NOT NULL,
  `decisions` bigint(20) unsigned NOT NULL,
  `flipped` bigint(20) unsigned NOT NULL,
  `varSetPos` bigint(20) unsigned NOT NULL,
  `varSetNeg` bigint(20) unsigned NOT NULL,
  `free` bigint(20) unsigned NOT NULL,
  `replaced` bigint(20) unsigned NOT NULL,
  `eliminated` bigint(20) unsigned NOT NULL,
  `set` bigint(20) unsigned NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `solverRun`
--

DROP TABLE IF EXISTS `solverRun`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `solverRun` (
  `runID` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `version` varchar(255) NOT NULL,
  `time` bigint(20) unsigned NOT NULL,
  PRIMARY KEY (`runID`)
) ENGINE=InnoDB AUTO_INCREMENT=45 DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Table structure for table `vars`
--

DROP TABLE IF EXISTS `vars`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `vars` (
  `runID` bigint(20) unsigned NOT NULL,
  `simplifications` bigint(20) unsigned NOT NULL,
  `order` int(10) unsigned NOT NULL,
  `pos` bigint(20) unsigned NOT NULL,
  `neg` bigint(20) unsigned NOT NULL,
  `total` bigint(20) unsigned NOT NULL,
  `flipped` bigint(20) unsigned NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2012-07-15 16:26:08
