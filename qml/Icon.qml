pragma ComponentBehavior: Bound
import QtQuick

Canvas {
    id: root
    property string name: "plus"
    property color color: "#a9b5c6"
    property real strokeWidth: 1.65
    implicitWidth: 20
    implicitHeight: 20
    antialiasing: true
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 24, height / 24)
        c.strokeStyle = color
        c.fillStyle = color
        c.lineWidth = strokeWidth
        c.lineCap = "round"
        c.lineJoin = "round"
        function line(x1,y1,x2,y2) { c.moveTo(x1,y1); c.lineTo(x2,y2) }
        function roundRect(x,y,w,h,r) {
            c.moveTo(x+r,y); c.lineTo(x+w-r,y); c.quadraticCurveTo(x+w,y,x+w,y+r)
            c.lineTo(x+w,y+h-r); c.quadraticCurveTo(x+w,y+h,x+w-r,y+h)
            c.lineTo(x+r,y+h); c.quadraticCurveTo(x,y+h,x,y+h-r)
            c.lineTo(x,y+r); c.quadraticCurveTo(x,y,x+r,y)
        }
        c.beginPath()
        switch(name) {
        case "plus": line(12,5,12,19); line(5,12,19,12); break
        case "close": line(6,6,18,18); line(18,6,6,18); break
        case "send": line(12,19,12,5); line(6,11,12,5); line(12,5,18,11); break
        case "stop": roundRect(6,6,12,12,2); c.fill(); break
        case "chevron": line(8,10,12,14); line(12,14,16,10); break
        case "right": line(9,6,15,12); line(15,12,9,18); break
        case "arrow": line(5,12,19,12); line(14,7,19,12); line(19,12,14,17); break
        case "check": line(5,12,10,17); line(10,17,19,7); break
        case "search": c.arc(10.5,10.5,6.5,0,Math.PI*2); line(15.5,15.5,21,21); break
        case "folder": c.moveTo(3,7); c.lineTo(3,19); c.lineTo(21,19); c.lineTo(21,7); c.lineTo(12,7); c.lineTo(10,4); c.lineTo(3,4); c.closePath(); break
        case "chat": roundRect(3,4,18,14,4); line(7,18,7,22); line(7,22,12,18); break
        case "panel": roundRect(3,4,18,16,3); line(15,4,15,20); break
        case "sidebar": roundRect(3,4,18,16,3); line(9,4,9,20); break
        case "copy": roundRect(8,8,12,13,2); line(15,4,5,4); line(5,4,5,16); break
        case "trash": line(4,6,20,6); line(9,3,15,3); c.moveTo(6,6); c.lineTo(7,21); c.lineTo(17,21); c.lineTo(18,6); line(10,10,10,17); line(14,10,14,17); break
        case "external": c.moveTo(13,4); c.lineTo(21,4); c.lineTo(21,12); line(21,4,11,14); c.moveTo(9,4); c.lineTo(4,4); c.lineTo(4,20); c.lineTo(20,20); c.lineTo(20,15); break
        case "download": line(12,3,12,15); line(7,10,12,15); line(12,15,17,10); c.moveTo(4,16); c.lineTo(4,21); c.lineTo(20,21); c.lineTo(20,16); break
        case "refresh": c.arc(12,12,8,0.5,5.5); c.moveTo(13,5); c.lineTo(18,6); c.lineTo(19,1); break
        case "globe": c.arc(12,12,9,0,Math.PI*2); c.moveTo(16,12); c.ellipse(8,3,8,18); line(3,12,21,12); break
        case "shield": c.moveTo(12,2); c.lineTo(21,6); c.lineTo(20,14); c.quadraticCurveTo(18,20,12,22); c.quadraticCurveTo(6,20,4,14); c.lineTo(3,6); c.closePath(); line(8,12,11,15); line(11,15,16,9); break
        case "spark": c.moveTo(12,2); c.lineTo(15,9); c.lineTo(22,12); c.lineTo(15,15); c.lineTo(12,22); c.lineTo(9,15); c.lineTo(2,12); c.lineTo(9,9); c.closePath(); break
        case "code": line(8,6,2,12); line(2,12,8,18); line(16,6,22,12); line(22,12,16,18); line(14,4,10,20); break
        case "file": c.moveTo(14,3); c.lineTo(5,3); c.lineTo(5,21); c.lineTo(19,21); c.lineTo(19,8); c.closePath(); line(14,3,14,8); line(14,8,19,8); line(9,12,15,12); line(9,16,15,16); break
        case "cpu": roundRect(6,6,12,12,2); roundRect(9,9,6,6,1); for(let i=8;i<18;i+=4){ line(i,2,i,6);line(i,18,i,22);line(2,i,6,i);line(18,i,22,i) } break
        case "sun": c.arc(12,12,4,0,Math.PI*2); for(let i=0;i<8;i++){const a=i*Math.PI/4; line(12+7*Math.cos(a),12+7*Math.sin(a),12+10*Math.cos(a),12+10*Math.sin(a))} break
        case "moon": c.moveTo(20,15); c.arc(12,12,9,0.35,4.55); c.quadraticCurveTo(8,15,20,15); break
        case "settings": c.arc(12,12,4,0,Math.PI*2); for(let i=0;i<8;i++){const a=i*Math.PI/4; line(12+7*Math.cos(a),12+7*Math.sin(a),12+10*Math.cos(a),12+10*Math.sin(a))} c.moveTo(19,12); c.arc(12,12,7,0,Math.PI*2); break
        case "info": c.arc(12,12,9,0,Math.PI*2); line(12,11,12,17); line(12,7,12,7.1); break
        case "lock": roundRect(5,10,14,11,3); c.moveTo(8,10); c.lineTo(8,7); c.arc(12,7,4,Math.PI,Math.PI*2); c.lineTo(16,10); line(12,14,12,17); break
        default: c.arc(12,12,3,0,Math.PI*2)
        }
        c.stroke()
    }
}
