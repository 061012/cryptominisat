// Stores the original X sizes of the graphs
// used when zooming out fully

//for graphs
var origSizes = new Array();
var blockRedraw = false;
var gs = new Array();

//For distibutions
var dists = [];

//For portal
var portal;

function setRollPeriod(num)
{
    for (var j = 0; j < myData.length; j++) {
        gs[j].updateOptions( {
            rollPeriod: num
        } );
    }
}

//Draws all graphs using dygraphs
function drawAllGraphs()
{
    for (var i = 0; i < myData.length; i++) {
        gs.push(drawOneGraph(i));
    }
}

//Draw one of the graphs
function drawOneGraph(i)
{
    graph = new Dygraph(
        document.getElementById(myData[i].dataDivID),
        myData[i].data
        , {
            stackedGraph: myData[i].stacked,
            includeZero: myData[i].stacked,
            labels: myData[i].labels,
            underlayCallback: function(canvas, area, g) {
                canvas.fillStyle = "rgba(105, 105, 185, 185)";
                //Draw simplification points
                colnum = myData[i].colnum;

                for(var k = 0; k < simplificationPoints[colnum].length-1; k++) {
                    var bottom_left = g.toDomCoords(simplificationPoints[colnum][k], -20);
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
                includeZero: true
              }
            },
            //stepPlot: true,
            //strokePattern: [0.1, 0, 0, 0.5],
            strokeWidth: 0.3,
            highlightCircleSize: 3,
            //rollPeriod: 1,
            drawXAxis: false,
            legend: 'always',
            xlabel: false,
            labelsDiv: document.getElementById(myData[i].labelDivID),
            labelsSeparateLines: true,
            labelsKMB: true,
            drawPoints: false,
            pointSize: 1,
            drawXGrid: false,
            drawYGrid: false,
            drawYAxis: false,
            strokeStyle: "black",
            colors: ['#ffffff', '#05fa03', '#d03332', '#4e4ea8', '#689696'],
            fillAlpha: 0.8,
            errorBars: myData[i].noisy,
            drawCallback: function(me, initial) {

                //Fill original sizes, so if we zoom out, we know where to
                //zoom out
                if (initial)
                    origSizes[myData[i].colnum] = me.xAxisRange();

                //Initial draw, ignore
                if (blockRedraw || initial)
                    return;

                blockRedraw = true;
                var xrange = me.xAxisRange();

                //Is this full reset?
                fullreset = false;
                for (var j = 0; j < myData.length; j++) {
                    if (gs[j] == me) {
                        if (origSizes[myData[j].colnum][0] == xrange[0]
                            &&origSizes[myData[j].colnum][1] == xrange[1]
                        ) {
                            fullreset = true;
                        }
                    }
                }

                /*
                //Must zoom the clause distribution as well
                if (fullreset) {
                    drawPattern(clauseDistrib[0], 0, origSizes[0][0], origSizes[0][1]);
                    drawPattern(clauseDistrib[1], 1, origSizes[1][0], origSizes[1][1]);
                } else {
                    drawPattern(clauseDistrib[0], 0, xrange[0], xrange[1]);
                    drawPattern(clauseDistrib[1], 1, xrange[0], xrange[1]);
                }*/

                //Zoom every one the same way
                for (var j = 0; j < myData.length; j++) {
                    //Don't go into loop
                    if (gs[j] == me)
                        continue;

                    //If this is a full reset, then zoom out maximally
                    if (fullreset) {
                        gs[j].updateOptions( {
                            dateWindow: origSizes[myData[j].colnum]
                        } );
                    } else {
                        gs[j].updateOptions( {
                            dateWindow: xrange
                        } );
                    }
                }

                blockRedraw = false;
            }
        }
    )

    return graph;
}

function drawAllDists()
{
    for(i = 0; i < columnDivs.length; i++) {
        a = new DrawClauseDistrib(
                clDistrib[i].data
                , clDistrib[i].canvasID
                , simplificationPoints[i]
            );
        a.drawPattern(0, maxConflRestart[i]);
        dists.push(a);
    }
}

function DrawClauseDistrib(_data, _divID, _simpPoints)
{
    var data = _data;
    var divID = _divID;
    var simpPoints = _simpPoints;
    var mywidth = 415;
    var myheight = 100;

    //For SVG pattern, a rectangle
    function drawDistribBox(x1, x2, y1, y2, relHeight, imgData)
    {
        var num = 255-relHeight*255.0;
        var type = "rgb(" + Math.floor(num) + "," + Math.floor(num) + "," + Math.floor(num) + ")";
        imgData.fillStyle = type;
        imgData.strokeStyle = type;
        imgData.fillRect(x1, y1, x2-x1, y2-y1);
    }

    function drawSimpLine(x1, x2, y1, y2, imgData)
    {
        imgData.strokeStyle = "rgba(105, 105, 185, 185)";
        imgData.fillStyle = "rgba(105, 105, 185, 185)";
        imgData.strokeRect(x1, y1, (x2-x1), (y2-y1));
    }

    function calcNumElemsVertical(from, to)
    {
        //Calculate highest point for this range
        var numElementsVertical = 0;
        for(var i = 0 ; i < data.length ; i ++ ){
            //out of range, ignore
            if (data[i].conflEnd < from) {
                continue;
            }
            if (data[i].conflStart > to) {
                break;
            }

            //Check which is the highest
            for(i2 = data[i].darkness.length-1; i2 >= 0 ; i2--) {
                if (data[i].darkness[i2] > 20) {
                    numElementsVertical = Math.max(numElementsVertical, i2);
                    break;
                }
            }
        }

        //alert(from + " " + to + " " + numElementsVertical + " " + i);
        return numElementsVertical;
    }

    this.drawPattern = function(from , to)
    {
        var myDiv = document.getElementById(divID);
        //myDiv.style.height = 100;
        //myDiv.style.width= 420;
        var ctx = myDiv.getContext("2d");
        var Xdelta = 0.5;

        var onePixelisConf = mywidth/(to-from);
        var numElementsVertical = calcNumElemsVertical(from, to);

        //Cut-off lines for Y
        var vAY = new Array();
        for(i = numElementsVertical; i >= 0; i--) {
            vAY.push(Math.round(i*(myheight/numElementsVertical)));
        }
        vAY.push(0);

        //Start drawing from X origin
        var lastXEnd = 0;
        var startFound = 0;
        for(var i = 0 ; i < data.length ; i ++) {
            if (startFound == 0 && data[i].conflEnd < from)
                continue;

            if (startFound == 1 && data[i].conflStart > to)
                continue;

            //Calculate maximum darkness
            maxDark = 0;
            for(i2 = 0; i2 < data[i].darkness.length; i2++) {
                maxDark = Math.max(maxDark, data[i].darkness[i2]);
            }

            var xStart = lastXEnd;

            var xEnd = data[i].conflEnd - from;
            xEnd *= onePixelisConf;
            xEnd += Xdelta;
            xEnd = Math.max(0, xEnd);
            xEnd = Math.min(xEnd, mywidth);
            xEnd = Math.round(xEnd);
            lastXEnd = xEnd;

            //Go through each Y component
            for(i2 = 0; i2 < data[i].darkness.length; i2++) {
                yStart = vAY[i2+1];
                yEnd   = vAY[i2];

                //How dark should it be?
                var darkness = 0;
                if (data[i].darkness[i2] != 0) {
                    darkness = data[i].darkness[i2]/maxDark;
                }

                //Draw the rectangle
                drawDistribBox(xStart, xEnd, yStart, yEnd, darkness, ctx);
            }
        }

        //Handle simplification lines
        for(var k = 0; k < simpPoints.length-1; k++) {
            var point = simpPoints[k] - from;
            point *= onePixelisConf;
            point += Xdelta;

            //Draw blue line
            if (point > 0) {
                drawSimpLine(point, point+1, 0, myheight, ctx);
            }
        }
    }
}

//Creates HTML for dygraphs
function createHTMLforGraphs()
{
    for (var i = 0; i < myData.length; i++) {
        datagraphs = document.getElementById("datagraphs");
        datagraphs.innerHTML += "\
        <div class=\"block\" id=\"" + myData[i].blockDivID + "\">\
        <table id=\"plot-table-a\">\
        <tr>\
        <td><div id=\"" + myData[i].dataDivID + "\" class=\"myPlotData\"></div></td>\
        <td><div id=\"" + myData[i].labelDivID + "\" class=\"draghandle\"></div></td>\
        </tr>\
        </table>\
        </div>";
    }
}

function createHTMLforDists()
{
    for(i = 0; i < clDistrib.length; i++) {
        datagraphs = document.getElementById("datagraphs");
        datagraphs.innerHTML += "\
        <div class=\"block\" id=\"" + clDistrib[i].blockDivID +"\"> \
        <table id=\"plot-table-a\"> \
        <tr> \
        <td> \
            <div id=\""+ clDistrib[i].dataDivID + "\" class=\"myPlotData\"> \
            <canvas id=\""+ clDistrib[i].canvasID + "\" width=\"420\" height=\"100\"> \
            no support for canvas</canvas> \
            </div> \
        </td> \
        <td> \
            <div id=\"" + clDistrib[i].labelDivID + "\" class=\"draghandle\"><b> \
            (" + i + ") Newly learnt clause size distribution. \
            Bottom: unitary clause. Top: largest clause. \
            Black: Many learnt. White: None learnt. \
            Horizontal resolution: 1000 conflicts. \
            Vertical resolution: 1 literal \
            </b></div> \
        </td> \
        </tr> \
        </table> \
        </div>";
    }
}

function createPortal()
{
    var settings = {};
    for(i = 0; i < columnDivs.length; i++) {
        tmp = Array();
        for(i2 = 0; i2 < columnDivs[i].length; i2++) {
            tmp.push(columnDivs[i][i2].blockDivID);
        }
        settings["column-" + i] = tmp;
    }
    var options = { portal : 'columns', editorEnabled : true};
    var data = {};
    Event.observe(window, 'load', function() {
            portal = new Portal(settings, options, data);
    });
}

function doAll()
{
    //Clear vars
    origSizes = new Array();
    blockRedraw = false;
    gs = new Array();
    dists = [];

    //Clear & create HTML
    datagraphs = document.getElementById("datagraphs");
    datagraphs.innerHTML = "";
    createHTMLforGraphs();
    createHTMLforDists();

    //Draws the graphs
    drawAllGraphs();
    drawAllDists();
    createPortal();
}

