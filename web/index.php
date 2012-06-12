<DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Cryptominisat</title>
    <style>

    @import url(//fonts.googleapis.com/css?family=Yanone+Kaffeesatz:400,700);
    @import url(style.css);
    @import url(tables/style.css);
    #example1         { min-height: 155px; }

    </style>
    <style type="text/css">
        .jqplot-data-label {
          /*color: #444;*/
    /*      font-size: 1.1em;*/
        }
    </style>

    <link rel="stylesheet" type="text/css" href="jquery.jqplot.css" />
    <script type="text/javascript" src="jquery/jquery.min.js"></script>
<!--     <script type="text/javascript" src="jquery/jquery.jqplot.min.js"></script> -->
<!--     <script type="text/javascript" src="jquery/plugins/jqplot.pieRenderer.min.js"></script> -->
<!--     <script type="text/javascript" src="jquery/plugins/jqplot.donutRenderer.min.js"></script> -->
    <script type="text/javascript" src="dygraphs/dygraph-dev.js"></script>
    <script type="text/javascript" src="highcharts/js/highcharts.js"></script>
<!--     <script type="text/javascript" src="highcharts/js/modules/exporting.js"></script> -->
</head>

<body>
<h1>Cryptominisat 3</h1>

<h3>Replacing wordy authority with visible certainty</h4>
<p>This webpage shows the solving of a SAT instance, visually. I was amazed by Edward Tufte's work while others in the office were amazed by a professional explaining the use of PowerPoint. Tufte would probably not approve, as these have a lot of chartjunk and the layout is terrible (pie charts are the worst offenders). However, it might allow you to understand SAT better, and may offer inspiration... or, rather, <i>vision</i>. Enjoy.</p>


<!--<p>Please select averaging level:
<input type="range" min="1" max="21" value="1" step="1" onchange="showValue(this.value)"/>
<span id="range">1</span>
</p>-->
<h2>Search restart statistics</h2>
<p>Below you will find conflicts in the X axis and several interesting data on the Y axis. You may zoom in by clicking on an interesting point and dragging the cursor along the X axis. Double-click to unzoom. Blue vertical lines indicate the positions of simplification sessions. Between the blue lines are what I call search sessions. Observe how time jumps at these blue lines, but, more importantly, observe how the behaviour of the solver changes drastically not much after the simplification session. Use the zoom: there is more than meets the eye.</p></br>

<script type="text/javascript">
function showValue(newValue)
{
    document.getElementById("range").innerHTML=newValue;
    setRollPeriod(newValue);
}
</script>


<script type="text/javascript">
var myDataFuncs=new Array();
var myDataNames=new Array();
</script>
<?
$runID = 456297562;
//$runID = 657697659;
//$runID = 3348265795;
//error_reporting(E_ALL);
error_reporting(E_STRICT);

$username="presenter";
$password="presenter";
$database="cryptoms";

mysql_connect(localhost, $username, $password);
@mysql_select_db($database) or die( "Unable to select database");

$query="
SELECT *
FROM `restart`
where `runID` = $runID
order by `conflicts`";

$result=mysql_query($query);
if (!$result) {
    die('Invalid query: ' . mysql_error());
}
$nrows=mysql_numrows($result);

function printOneThing($name, $nicename, $data, $nrows)
{
    $fullname = $name."Data";
    $nameLabel = $name."Label";
    echo "<tr><td>
    <div id=\"$name\" class=\"myPlotData\"></div>
    </td><td valign=top>
    <div id=\"$nameLabel\" class=\"myPlotLabel\"></div>
    </td></tr>";
    echo "<script type=\"text/javascript\">
    function $fullname() {
    return \"Conflicts,$nicename\\n";
    $i=0;
    while ($i < $nrows) {
        $conf=mysql_result($data, $i, "conflicts");
        $b=mysql_result($data, $i, $name);
        if ($i == $nrows-1) {
            echo "$conf, $b;\"};\n";
        } else {
            echo "$conf, $b\\n";
        }
        $i++;
    }
    echo "myDataFuncs.push($fullname);\n";
    echo "myDataNames.push(\"$name\");\n";
    echo "</script>\n";
}

echo "<table id=\"plot-table-a\">";
printOneThing("time", "time", $result, $nrows);
printOneThing("restarts", "restarts", $result, $nrows);
printOneThing("branchDepth", "branch depth", $result, $nrows);
printOneThing("branchDepthDelta", "branch depth delta", $result, $nrows);
printOneThing("trailDepth", "trail depth", $result, $nrows);
printOneThing("trailDepthDelta", "trail depth delta", $result, $nrows);
printOneThing("glue", "glue", $result, $nrows);
printOneThing("size", "size", $result, $nrows);
printOneThing("resolutions", "resolutions", $result, $nrows);

$query="
SELECT `conflicts`, `replaced`, `set`
from `vars`
where `runID` = $runID
order by `conflicts`";
$result=mysql_query($query);
if (!$result) {
    die('Invalid query: ' . mysql_error());
}
$nrows=mysql_numrows($result);
printOneThing("set", "vars set", $result, $nrows);
printOneThing("replaced", "vars replaced", $result, $nrows);
echo "</table>";

$query="
SELECT max(conflicts) as confl, simplifications as simpl
FROM restart
where runID = $runID
group by simplifications";

$result=mysql_query($query);
if (!$result) {
    die('Invalid query: ' . mysql_error());
}
$nrows=mysql_numrows($result);

function getAnnotations($name, $result, $nrows)
{
    //echo "gs[0].setAnnotations([";

    echo "myAnnotations = [";
    $i=0;
    while ($i < $nrows) {
        $conf=mysql_result($result, $i, "confl");
        $b=mysql_result($result, $i, "simpl");

        echo "{series: \"$name\",
        x: \"$conf\"
        , shortText: \"S$b\"
        , text: \"Simplification no. $b\"
        },";
        $i++;
    }
    echo "];";
    echo "myDataAnnotations.push(myAnnotations);\n";

    echo "simplificationPoints = [";
    $i=0;
    while ($i < $nrows) {
        $conf=mysql_result($result, $i, "confl");
        echo "$conf";
        $i++;
        if ($i < $nrows) {
            echo ", ";
        }
    }
    echo "];";
}
echo "<script type=\"text/javascript\">";
echo "var myDataAnnotations=new Array();";
getAnnotations("time", $result, $nrows);
/*getAnnotations("restarts", $result, $nrows);
getAnnotations("branch depth", $result, $nrows);
getAnnotations("branch depth delta", $result, $nrows);
getAnnotations("trail depth", $result, $nrows);
getAnnotations("trail depth delta", $result, $nrows);
getAnnotations("glue", $result, $nrows);
getAnnotations("size", $result, $nrows);
getAnnotations("resolutions", $result, $nrows);*/
echo "</script>\n";
?>

<script type="text/javascript">
function todisplay(i,len)
{
if (i == len-1)
    return "Conflicts";
else
    return "";
};

gs = [];
var blockRedraw = false;
for (var i = 0; i <= myDataNames.length; i++) {
    gs.push(new Dygraph(
        document.getElementById(myDataNames[i]),
        myDataFuncs[i]
        , {
            underlayCallback: function(canvas, area, g) {
                canvas.fillStyle = "rgba(185, 185, 245, 245)";
                for(var k = 0; k < simplificationPoints.length; k++) {
                    var bottom_left = g.toDomCoords(simplificationPoints[k], -20);
                    var left = bottom_left[0];
                    canvas.fillRect(left, area.y, 2, area.h);
                }
            },
            axes: {
              x: {
                valueFormatter: function(d) {
                  return 'Conflicts: ' + d;
                },
                pixelsPerLabel: 100,
              }
            },
            //stepPlot: true,
            strokePattern: [0.1, 0, 0, 0.5],
            strokeWidth: 2,
            highlightCircleSize: 3,
            rollPeriod: 1,
            drawXAxis: i == myDataNames.length-1,
            legend: 'always',
            xlabel: todisplay(i, myDataNames.length),
            labelsDivStyles: {
                'text-align': 'right',
                'background': 'none'
            },
            labelsDiv: document.getElementById(myDataNames[i]+"Label"),
            labelsSeparateLines: true,
            labelsKMB: true,
            drawPoints: true,
            pointSize: 1.5,
            //errorBars: false,
            drawCallback: function(me, initial) {
                if (blockRedraw || initial)
                    return;

                blockRedraw = true;
                var xrange = me.xAxisRange();
                //var yrange = me.yAxisRange();
                for (var j = 0; j < myDataNames.length; j++) {
                    if (gs[j] == me)
                        continue;

                    gs[j].updateOptions( {
                        dateWindow: xrange
                    } );
                }
                blockRedraw = false;
            }
        }
    ));
}

/*for (i = 0; i <= myDataAnnotations.length; i++)  {
    gs[i].setAnnotations(myDataAnnotations[i]);
}*/

function setRollPeriod(num)
{
    for (var j = 0; j < myDataNames.length; j++) {
        gs[j].updateOptions( {
            rollPeriod: num
        } );
    }
}
</script>

<script type="text/javascript">
var clauseStatsData=new Array();
clauseStatsData.push(new Array());
clauseStatsData.push(new Array());
</script>

<h2>Clause statistics before each clause database cleaning</h2>
<?
function createDataClauseStats($reduceDB, $runID, $learnt)
{
    $query="
    SELECT sum(`numPropAndConfl`) as mysum, avg(`numPropAndConfl`) as myavg, count(`numPropAndConfl`) as mycnt, `size`
    FROM `clauseStats`
    where `runID` = $runID and `reduceDB`= $reduceDB and `learnt` = $learnt
    and `numPropAndConfl` > 0
    group by `size`
    order by `size`
    limit 200";

    $result=mysql_query($query);
    if (!$result) {
        die('Invalid query: ' . mysql_error());
    }
    $nrows=mysql_numrows($result);

    echo "clauseStatsData[$learnt].push([\n";
    $i=0;
    $numprinted = 0;
    while ($i < $nrows) {
        $myavg=mysql_result($result, $i, "myavg");
        $mysum=mysql_result($result, $i, "mysum");
        $mycnt=mysql_result($result, $i, "mycnt");
        $size=mysql_result($result, $i, "size");
        if ($mycnt < 10) {
            $i++;
            continue;
        }
        $numprinted++;
        if ($numprinted > 1) {
            echo ",";
        }

        echo "[$size, $myavg]\n";

        $i++;
    }
    echo "]);\n";
}

//Get maximum number of simplifications
$query="
SELECT max(reduceDB) as mymax
FROM `clauseStats`
where `runID` = $runID";
$result=mysql_query($query);
if (!$result) {
    die('Invalid query: ' . mysql_error());
}
$maxNumReduceDB = mysql_result($result, 0, "mymax");

echo "<script type=\"text/javascript\">\n";
for($i = 1; $i < $maxNumReduceDB; $i++) {
    for($learnt = 1; $learnt < 2; $learnt++) {
        createDataClauseStats($i, $runID, $learnt);
    }
}
echo "</script>\n";

echo "<table id=\"plot-table-a\">";
for($i = 1; $i < $maxNumReduceDB; $i++) {
    for($learnt = 1; $learnt < 2; $learnt++) {
        echo "<tr><td>
        <div id=\"clauseStatsPlot$i-$learnt\" class=\"myPlotData\"></div>
        </td><td valign=top>
        <div id=\"clauseStatsPlotLabel$i-$learnt\" class=\"myPlotLabel\"></div>
        </td></tr>";
    }
}
echo "</tr></table>";
?>

<script type="text/javascript">
for(learnt = 1; learnt < 2; learnt++) {
    for(i = 0; i < clauseStatsData[learnt].length; i++) {
        var i2 = i+1;
        var gzz = new Dygraph(
            document.getElementById('clauseStatsPlot' + i2 + '-' + learnt),
            clauseStatsData[learnt][i],
            {
                drawXAxis: i == clauseStatsData[learnt].length-1,
                legend: 'always',
                labels: ['size', 'num prop&confl'],
                connectSeparatedPoints: true,
                drawPoints: true,
                labelsDivStyles: {
                    'text-align': 'right',
                    'background': 'none'
                },
                labelsDiv: document.getElementById('clauseStatsPlotLabel'+ i2 + '-' + learnt),
                labelsSeparateLines: true,
                labelsKMB: true
                //,title: "Most propagating&conflicting clauses before clause clean " + i
            }
        );
    }
}
var varPolarsData = new Array();
</script>



<h2>Variable statistics for each search session</h2>
<p> These graphs show how many times the topmost set variables were set to positive or negative polarity. Also, it shows how many times they were flipped, relative to their stored, old polarity.</p>
<?
function createDataVarPolars($simpnum, $runID)
{
    $query="
    SELECT *
    FROM `polarSet`
    where `runID` = $runID and `simplifications`= $simpnum
    order by `order`
    limit 200";

    $result=mysql_query($query);
    if (!$result) {
        die('Invalid query: ' . mysql_error());
    }
    $nrows=mysql_numrows($result);

    echo "varPolarsData.push([\n";
    $i=0;
    while ($i < $nrows) {
        $order=mysql_result($result, $i, "order");
        $pos=mysql_result($result, $i, "pos");
        $neg=mysql_result($result, $i, "neg");
        $total=mysql_result($result, $i, "total");
        $flipped=mysql_result($result, $i, "flipped");

        echo "[$order, $pos, $neg, $total, $flipped]\n";

        $i++;
        if ($i < $nrows) {
            echo ",";
        }
    }
    echo "]);\n";
}

//Get maximum number of simplifications
$query="
SELECT max(simplifications) as mymax
FROM `polarSet`
where `runID` = $runID";
$result=mysql_query($query);
if (!$result) {
    die('Invalid query: ' . mysql_error());
}
$maxNumSimp = mysql_result($result, 0, "mymax");

echo "<script type=\"text/javascript\">\n";
for($i = 1; $i < $maxNumSimp; $i++) {
    createDataVarPolars($i, $runID);
}
echo "</script>\n";

echo "<table id=\"plot-table-a\">";
for($i = 1; $i < $maxNumSimp; $i++) {
    echo "<tr><td>
    <div id=\"varPolarsPlot$i\" class=\"myPlotData2\"></div>
    </td><td valign=top>
    <div id=\"varPolarsPlotLabel$i\" class=\"myPlotLabel\"></div>
    </td></tr>";
}
echo "</tr></table>";
?>

<script type="text/javascript">
for(i = 0; i < varPolarsData.length; i++) {
    var i2 = i+1;
    var gzz = new Dygraph(
        document.getElementById('varPolarsPlot' + i2),
        varPolarsData[i],
        {
            legend: 'always',
            labels: ['no.', 'pos polar', 'neg polar', 'total set', 'flipped polar' ],
            connectSeparatedPoints: true,
            drawPoints: true,
            labelsDivStyles: {
                'text-align': 'right',
                'background': 'none'
            },
            labelsDiv: document.getElementById('varPolarsPlotLabel'+ i2),
            labelsSeparateLines: true,
            labelsKMB: true
//             ,xlabel: "Top 200 most set variables",
            //,title: "Most set variables during search session " + i
        }
    );
}
</script>


<h2>Search statistics for each session</h2>
<p>Here are some pie charts detailing propagations and other stats for each search. "red." means reducible, also called "learnt". "irred." means irreducible, also called "non-learnt" (terminology by A. Biere).</p>
<?
function getLearntData($runID)
{
    print "<script type=\"text/javascript\">";
    //Get data for learnts
    $query="
    SELECT *
    FROM `learnts`
    where `runID` = $runID
    order by `simplifications`";

    //Gather results
    $result=mysql_query($query);
    if (!$result) {
        die('Invalid query: ' . mysql_error());
    }
    $nrows=mysql_numrows($result);


    //Write learnt data to 'learntData'
    $i=0;
    echo "var learntData = [ ";
    $divplacement = "";
    while ($i < $nrows) {
        $units=mysql_result($result, $i, "units");
        $bins=mysql_result($result, $i, "bins");
        $tris=mysql_result($result, $i, "tris");
        $longs=mysql_result($result, $i, "longs");

        echo "[ ['unit', $units],['bin', $bins],['tri', $tris],['long', $longs] ]\n";

        if ($i != $nrows-1) {
            echo " , ";
        }
        $i++;
    }
    echo "];\n";
    echo "</script>";

    return $nrows;
}

function getPropData($runID)
{
    print "<script type=\"text/javascript\">";
    //Get data for learnts
    $query="
    SELECT *
    FROM `props`
    where `runID` = $runID
    order by `simplifications`";

    //Gather results
    $result=mysql_query($query);
    if (!$result) {
        die('Invalid query: ' . mysql_error());
    }
    $nrows=mysql_numrows($result);


    //Write prop data to 'propData'
    $i=0;
    echo "var propData = [ ";
    $divplacement = "";
    while ($i < $nrows) {
        $unit=mysql_result($result, $i, "unit");
        $binIrred=mysql_result($result, $i, "binIrred");
        $binRed=mysql_result($result, $i, "binRed");
        $tri=mysql_result($result, $i, "tri");
        $longIrred=mysql_result($result, $i, "longIrred");
        $longRed=mysql_result($result, $i, "longRed");

        echo "[ ['unit', $unit],['bin irred.', $binIrred],['bin red.', $binRed]
        , ['tri', $tri],['long irred.', $longIrred],['long red.', $longRed] ]\n";

        if ($i != $nrows-1) {
            echo " , ";
        }
        $i++;
    }
    echo "];\n";
    echo "</script>";

    return $nrows;
}

function getConflData($runID)
{
    print "<script type=\"text/javascript\">";
    //Get data for learnts
    $query="
    SELECT *
    FROM `confls`
    where `runID` = $runID
    order by `simplifications`";

    //Gather results
    $result=mysql_query($query);
    if (!$result) {
        die('Invalid query: ' . mysql_error());
    }
    $nrows=mysql_numrows($result);


    //Write prop data to 'propData'
    $i=0;
    echo "var conflData = [ ";
    $divplacement = "";
    while ($i < $nrows) {
        $binIrred=mysql_result($result, $i, "binIrred");
        $binRed=mysql_result($result, $i, "binRed");
        $tri=mysql_result($result, $i, "tri");
        $longIrred=mysql_result($result, $i, "longIrred");
        $longRed=mysql_result($result, $i, "longRed");

        echo "[ ['bin irred.', $binIrred] ,['bin red.', $binRed],['tri', $tri]
        ,['long irred.', $longIrred],['long red.', $longRed] ]\n";

        if ($i != $nrows-1) {
            echo " , ";
        }
        $i++;
    }
    echo "];\n";
    echo "</script>";

    return $nrows;
}

$nrows = getLearntData($runID);
getPropData($runID);
getConflData($runID);


//End script, create tables
function createTable($nrows)
{
    $height = 150;
    $width = 150;
    $i = 0;
    echo "<table id=\"box-table-a\">\n";
    echo "<tr><th>Search session</th><th>Learnt Clause type</th><th>Propagation by</th><th>Conflicts by</th></tr>\n";
    while ($i < $nrows) {
        echo "<tr>\n";
        echo "<td width=\"1%\">$i</td>\n";
        echo " <td width=\"30%\"><div id=\"learnt$i\" style=\"min-height:".$height."px; min-width:".$width."px;\"></div></td>\n";
        echo " <td width=\"30%\"><div id=\"prop$i\" style=\"min-height:".$height."px; min-width:".$width."px;\"></div></td>\n";
        echo " <td width=\"30%\"><div id=\"confl$i\" style=\"min-height:".$height."px; min-width:".$width."px;\"></div></td>\n";
        echo "</tr>\n";
        $i++;
    };
    echo "</table>\n";
}
createTable($nrows);
mysql_close();
?>
</script>

<script type="text/javascript">
function drawChart(name, num, data) {
    chart = new Highcharts.Chart(
    {
        chart: {
            renderTo: name + num,
            plotBackgroundColor: null,
            plotBorderWidth: null,
            plotShadow: false,
            spacingTop: 30,
            spacingRight: 30,
            spacingBottom: 30,
            spacingLeft: 30
        },
        title: {
            text: ''
        },
        tooltip: {
            formatter: function() {
                return '<b>'+ this.point.name +'</b>: '+ this.y + '(' + this.percentage.toFixed(1) + '%)';
            }
        },
        credits: {
            enabled: false
        },
        plotOptions: {
            pie: {
                allowPointSelect: true,
                cursor: 'pointer',
                dataLabels: {
                    enabled: true,
                    color: '#000000',
                    distance: 30,
                    connectorColor: '#000000',
                    /*formatter: function() {
                        return '<b>'+ this.point.name +'</b>: '+ this.percentage +' %';
                    },*/
                    overflow: "justify"
                }
            }
        },
        series: [{
            type: 'pie',
            name: 'Learnt clause types',
            data: data[num]
        }],
        exporting: {
            enabled: false
        }
    });
};

var chart;
for(i = 0; i < learntData.length; i++) {
    drawChart("learnt", i, learntData);
}

for(i = 0; i < propData.length; i++) {
    drawChart("prop", i, propData);
}

for(i = 0; i < conflData.length; i++) {
    drawChart("confl", i, conflData);
}
</script>


<!--<div id="fig" style="width:20px; height:20px"></div>
<script type="text/javascript" src="mbostock-protovis-1a61bac/protovis.min.js"></script>
<script type="text/javascript+protovis">

// Sizing and scales
var data = pv.range(5).map(Math.random);
var w = 200,
    h = 200,
    r = w / 2,
    a = pv.Scale.linear(0, pv.sum(data)).range(0, 2 * Math.PI);

// The root panel.
var vis = new pv.Panel()
    .width(w)
    .height(h);

// The wedge, with centered label.
vis.add(pv.Wedge)
    .data(data.sort(pv.reverseOrder))
    .bottom(w / 2)
    .left(w / 2)
    .innerRadius(r - 40)
    .outerRadius(r)
    .angle(a)
    .event("mouseover", function() this.innerRadius(0))
    .event("mouseout", function() this.innerRadius(r - 40))
    .anchor("center").add(pv.Label)
    .visible(function(d) d > .15)
    .textAngle(0)
    .text(function(d) d.toFixed(2));

vis.render();
</script>-->

<!--<div id="example1"></div>

<script src="d3/d3.v2.js"></script>
<script src="cubism/cubism.v1.js"></script>
<script>
function myvalues(name) {
  var value = 0,
      values = [],
      i = 0,
      last;
  return context.metric(function(start, stop, step, callback) {
    start = +start;
    stop = +stop;
    if (isNaN(last))
        last = start;

    while (last < stop) {
      last += step;
      //value = Math.max(-10, Math.min(10, value + .8 * Math.random() - .4 + .2 * Math.cos(i += .2)));
      values.push(10);
    }
    callback(null, values = values.slice((start - stop) / step));
  }, name);
}

var context = cubism.context()
    .serverDelay(0)
    .clientDelay(0)
    //.step(10)
    .size(800);

//    var foo = random("glue");
var foo = myvalues("glue");

d3.select("#example1").call(function(div) {

  div.append("div")
      .attr("class", "axis")
      .call(context.axis().orient("top"));

  div.selectAll(".horizon")
      .data([foo])
    .enter().append("div")
      .attr("class", "horizon")
      .call(context.horizon().extent([-20, 20]));

  div.append("div")
      .attr("class", "rule")
      .call(context.rule());

});

// On mousemove, reposition the chart values to match the rule.
context.on("focus", function(i) {
  d3.selectAll(".value").style("right", i == null ? null : context.size() - i + "px");
});
</script>-->


</html>


