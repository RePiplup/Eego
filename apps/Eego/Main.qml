// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
ApplicationWindow {
 id: win
 width: 1060; height: 760; minimumWidth: 690; minimumHeight: 550
 visible: true; title: qsTr("鹅鹅下载 · 统一版"); color: "white"
 font.family: "Microsoft YaHei UI"; font.pixelSize: 13
 property string category: "all"
 readonly property var categoryLabels: ({all:qsTr("全部任务"),active:qsTr("下载中"),paused:qsTr("已暂停"),completed:qsTr("已完成"),issues:qsTr("有异常")})
 property int selectedId: -1
 property var selectedIds: []
 property int anchorId: -1
 property var removeTargets: []
 property bool viewFilesOnly: false
 property bool receivedList:false
 Component.onCompleted:{latest=downloadService.startupTasks || [];reconcile()}
 readonly property bool canOperate: receivedList && downloadService.available && downloadService.busyId < 0 && !closingService
 function visibleIds(){let a=[];for(let i=0;i<rows.count;i++)a.push(rows.get(i).taskId);return a}
 function setSelection(ids, current){selectedIds=ids;selectedId=ids.includes(current)?current:(ids.length?ids[ids.length-1]:-1)}
 function pick(id, modifiers){
  const ids=visibleIds(),pos=ids.indexOf(id),anchor=ids.indexOf(anchorId)
  if((modifiers & Qt.ShiftModifier)&&anchor>=0){const range=ids.slice(Math.min(pos,anchor),Math.max(pos,anchor)+1);setSelection((modifiers & Qt.ControlModifier)?Array.from(new Set(selectedIds.concat(range))):range,id)}
  else if(modifiers & Qt.ControlModifier){setSelection(selectedIds.includes(id)?selectedIds.filter(x=>x!==id):selectedIds.concat([id]),id);anchorId=id}
  else {setSelection([id],id);anchorId=id}
 }
 function selectAll(){setSelection(visibleIds(),selectedId);if(anchorId<0)anchorId=selectedId}
 function eligible(op){return latest.some(t=>selectedIds.includes(t.id)&&t.capabilities[op])}
 function batch(op){if(canOperate){errorText="";downloadService.batch(op,selectedIds)}}
 function askRemove(){if(!canOperate||!eligible("remove"))return;removeTargets=latest.filter(t=>selectedIds.includes(t.id)&&t.capabilities.remove).map(t=>({id:t.id,name:t.name,path:t.path}));removeDialog.open();cancelRemove.forceActiveFocus()}
 function context(id,x,y){if(id<0)setSelection([],-1);else if(!selectedIds.includes(id))pick(id,0);list.forceActiveFocus();taskMenu.popup(x,y)}
 function showFiles(t){
  viewFilesOnly=!t.capabilities.selectFiles
  selection.taskId=t.id
  selection.order=[]
  selection.folderPanel=false
  selection.sortMode=0
  fileChoices.clear()
  for(const f of t.files)
   fileChoices.append({fileIndex:f.index,path:f.path,normalizedPath:f.path.replace(/\\/g,"/"),bytes:f.size||0,chosen:f.selected,kind:selection.fileKind(f.path)})
  selection.recount()
  selection.folders=Object.keys(selection.totals.folders).sort()
  selection.sortFiles(0)
  selection.open()
 }
 function openTask(t,folder){
  if(!t)return
  const files=t.url.startsWith("magnet:?")?t.files.filter(f=>f.selected):[]
  if(files.length===1)downloadService.openPath(t.id,folder,files[0].index)
  else if(!folder&&t.url.startsWith("magnet:?"))showFiles(t)
  else downloadService.openPath(t.id,folder)
 }
 property var latest: []
 property string errorText: ""
 property bool closingService: false
 property bool submitting: false
 readonly property color blue: "#416daf"
 readonly property color edge: "#e2e8f0"
 readonly property color ink: "#1d2d46"
 readonly property color muted: "#64748b"
 readonly property color soft: "#f5f7fb"
 readonly property var selected: latest.find(t => t.id === selectedId) || null
 readonly property bool narrow: width < 800
 readonly property int actionColumnWidth: category==="completed"?222:73
 readonly property string totalSpeed:sizeText(latest.reduce((sum,t)=>sum+(t.status==="downloading" ? (t.speed||0) : 0),0))+"/s"
 function exitApplication(){if(!closingService){closingService=true;downloadService.shutdown()}}
 onClosing: close => {
  close.accepted=false
  if(!closingService)closeChoice.open()
 }
 function sizeText(n) {
  if(n===null||n===undefined||n<0)return qsTr("大小未知")
  let u=["B","KiB","MiB","GiB","TiB"],i=0
  while(n>=1024&&i<4){n/=1024;++i}
  return n.toFixed(i?1:0)+" "+u[i]
 }
 function status(t) {
  if(t.status==="syncing")return qsTr("正在读取任务列表")
  if(downloadService.busyId===t.id)return qsTr("正在提交")
  if(t.status==="removing")return qsTr("正在停止并移除")
  if(t.pauseRequested)return qsTr("正在暂停")
  return ({completed:qsTr("已完成"),paused:qsTr("已暂停"),failed:qsTr("下载失败"),queued:qsTr("等待中"),downloading:qsTr("下载中"),fetching:t.phase===2?qsTr("校验中"):qsTr("连接中"),choosing:qsTr("待选文件")})[t.status]||t.status
 }
 function group(t,n) {
  return n==="all"||n==="active"&&["downloading","fetching","queued","choosing"].includes(t.status)||n==="paused"&&t.status==="paused"||n==="completed"&&t.status==="completed"||n==="issues"&&t.status==="failed"
 }
 function actionName(t) {
  if(downloadService.busyId===t.id||t.pauseRequested)return qsTr("提交中")
  if(t.capabilities.selectFiles)return qsTr("选择文件")
  return t.capabilities.pause?qsTr("暂停"):t.capabilities.resume?qsTr("继续"):t.capabilities.retry?qsTr("重试"):qsTr("详情")
 }
 function act(t) {
  setSelection([t.id],t.id);anchorId=t.id;errorText=""
  if(t.capabilities.selectFiles){showFiles(t)}
  else if(t.capabilities.pause)downloadService.action("pause",t.id)
  else if(t.capabilities.resume)downloadService.action("resume",t.id)
  else if(t.capabilities.retry)downloadService.action("retry",t.id)
  else detail.open()
 }
 function reconcile() {
  const next=latest.filter(t=>group(t,category)&&t.name.toLowerCase().includes(search.text.trim().toLowerCase()))
  for(let i=0;i<next.length;++i){
   const t=next[i],r={taskId:t.id,fileName:t.name,taskData:JSON.stringify(t)}
   if(i>=rows.count||rows.get(i).taskId!==t.id){
    let j=i+1;while(j<rows.count&&rows.get(j).taskId!==t.id)++j
    if(j<rows.count)rows.move(j,i,1);else rows.insert(i,r)
   }
   for(const k in r)if(rows.get(i)[k]!==r[k])rows.setProperty(i,k,r[k])
  }
  if(rows.count>next.length)rows.remove(next.length,rows.count-next.length)
  const ids=next.map(t=>t.id);setSelection(selectedIds.filter(id=>ids.includes(id)),selectedId);if(!ids.includes(anchorId))anchorId=-1
 }
 ListModel{id:rows}
 Connections {
  target:downloadService
  function onUpdated(tasks){win.receivedList=true;win.latest=tasks;win.reconcile()}
  function onProblem(message){win.submitting=false;win.errorText=message}
  function onCreated(id){win.submitting=false;composer.close();entry.clear();win.category="all";search.clear();win.setSelection([id],id);win.anchorId=id}
 }
 component Glyph: Item {
  id: glyph
  property string name: "file"
  implicitWidth:20;implicitHeight:20
  Image { anchors.fill:parent;source:"assets/"+glyph.name+".svg";sourceSize.width:24;sourceSize.height:24;fillMode:Image.PreserveAspectFit }
 }
 component Action: Button {
  id:btn;property bool primary:false
  implicitHeight:36;leftPadding:12;rightPadding:12;font.pixelSize:12
  contentItem:Text{text:btn.text;font:btn.font;color:!btn.enabled?"#98a3b2":btn.primary?"white":win.ink;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
  background:Rectangle{radius:8;border.width:btn.activeFocus?2:1;border.color:btn.activeFocus?win.blue:btn.primary?win.blue:win.edge;color:!btn.enabled?"#f1f3f7":btn.primary?(btn.down?"#2b4f83":btn.hovered?"#355f9d":win.blue):(btn.down?"#dae5f5":btn.hovered?"#edf2fa":"white")}
 }
 component Sheet: Dialog {
  id:sheet;modal:true;padding:24;anchors.centerIn:parent;closePolicy:Popup.CloseOnEscape
  background:Rectangle{radius:12;color:"white";border.color:win.edge}
  header:RowLayout{
   Label{text:sheet.title;font.pixelSize:18;font.bold:true;color:win.ink;Layout.fillWidth:true;Layout.leftMargin:24;Layout.topMargin:16}
   Action{text:qsTr("关闭");Accessible.name:qsTr("关闭")+sheet.title;Layout.topMargin:12;Layout.rightMargin:16;onClicked:sheet.close()}
  }
  Overlay.modal:Rectangle{color:"#661b2947"}
 }
 ColumnLayout {
  anchors.fill:parent;spacing:0
  RowLayout {
   Layout.fillWidth:true;Layout.fillHeight:true;spacing:0
   Rectangle {
    Layout.preferredWidth:win.narrow?72:176;Layout.fillHeight:true;color:win.soft
    Rectangle{anchors.right:parent.right;height:parent.height;width:1;color:win.edge}
    ColumnLayout {
     anchors.fill:parent;anchors.margins:win.narrow?10:12;spacing:5
     RowLayout {
      Layout.topMargin:16;Layout.bottomMargin:25;Layout.leftMargin:win.narrow?2:8;spacing:8
      Image{source:"assets/eego-nitro.png";sourceSize.width:128;sourceSize.height:128;Layout.preferredWidth:win.narrow?44:48;Layout.preferredHeight:48;fillMode:Image.PreserveAspectFit}
      ColumnLayout{
       visible:!win.narrow;spacing:2
       Label{text:qsTr("鹅鹅下载");font.pixelSize:15;font.bold:true;color:win.ink}
       Label{text:qsTr("闪电主题");font.pixelSize:11;color:win.muted}
      }
     }
     Repeater {
      model:[{key:"all",label:win.categoryLabels.all,icon:"inbox"},{key:"active",label:win.categoryLabels.active,icon:"download"},{key:"paused",label:win.categoryLabels.paused,icon:"pause"},{key:"completed",label:win.categoryLabels.completed,icon:"check"},{key:"issues",label:win.categoryLabels.issues,icon:"alert"}]
      delegate:ItemDelegate {
       id:nav;required property var modelData
       Layout.fillWidth:true;implicitHeight:42;Accessible.name:modelData.label
       checked:win.category===modelData.key
       onClicked:{win.category=modelData.key;win.setSelection([],-1);win.anchorId=-1;win.reconcile()}
       ToolTip.visible:hovered&&win.narrow;ToolTip.text:modelData.label
       contentItem:RowLayout{
        spacing:9;Glyph{name:nav.modelData.icon;Layout.alignment:Qt.AlignHCenter}
        Label{visible:!win.narrow;text:nav.modelData.label;color:nav.checked?win.blue:win.ink;font.bold:nav.checked;Layout.fillWidth:true}
        Label{visible:!win.narrow;text:win.latest.filter(t=>win.group(t,nav.modelData.key)).length;color:win.muted;font.pixelSize:11}
       }
       background:Rectangle{radius:8;border.color:nav.activeFocus?win.blue:"transparent";color:nav.checked?"#e8f0fc":nav.hovered?"#edf1f7":"transparent"}
      }
     }
     Item{Layout.fillHeight:true}
     ItemDelegate {
      id: settingsButton
      Layout.fillWidth:true;implicitHeight:42;Accessible.name:qsTr("设置");onClicked:settings.open()
      contentItem:RowLayout{Glyph{name:"settings"} Label{visible:!win.narrow;text:qsTr("设置");color:win.muted;Layout.fillWidth:true}}
      background:Rectangle{radius:8;color:settingsButton.hovered?"#e8f0fc":"transparent";border.color:settingsButton.activeFocus?win.blue:"transparent"}
     }
    }
   }
   ColumnLayout {
    Layout.fillHeight:true;Layout.fillWidth:true;Layout.margins:win.narrow?18:27;spacing:16
    RowLayout {
     Layout.fillWidth:true
     ColumnLayout{
      Layout.fillWidth:true;spacing:5
      Label{text:win.categoryLabels[win.category];font.pixelSize:22;font.bold:true;color:win.ink}
      Label{text:win.latest.filter(t=>["downloading","fetching"].includes(t.status)).length+qsTr(" 个正在下载 · ")+win.latest.filter(t=>t.status==="queued").length+qsTr(" 个等待中");font.pixelSize:12;color:win.muted}
     }
     Item { Layout.fillWidth:true }
     Rectangle{
      implicitWidth:rate.width+28;implicitHeight:48;radius:12;color:"#e9f3fa"
      Column{id:rate;anchors.centerIn:parent
       Label{text:qsTr("总下载速度");color:"#46657c";font.pixelSize:11}
       Label{text:win.totalSpeed;color:"#215c85";font.pixelSize:18;font.bold:true}
      }
     }
     Action{id:newButton;text:qsTr("新建任务");primary:true;enabled:downloadService.available&&!win.closingService;onClicked:{win.errorText="";composer.open();entry.forceActiveFocus()}}
    }
    TextField {
     id:search;objectName:"taskSearch";Layout.fillWidth:true;implicitHeight:36;leftPadding:35
     placeholderText:qsTr("搜索任务名称");Accessible.name:qsTr("搜索任务名称");selectByMouse:true;color:win.ink
     onTextChanged:win.reconcile()
     background:Rectangle{radius:8;color:win.soft;border.color:search.activeFocus?win.blue:win.edge;Glyph{name:"search";anchors.left:parent.left;anchors.leftMargin:10;anchors.verticalCenter:parent.verticalCenter;width:16;height:16}}
    }
    Label{visible:win.errorText!==""&&!composer.opened;text:win.errorText;color:"#a63f45";Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:12}
    ColumnLayout {
     Layout.fillWidth:true;Layout.fillHeight:true;spacing:0
     RowLayout {
      Layout.fillWidth:true;Layout.leftMargin:48;Layout.rightMargin:12;spacing:12
      Label{text:qsTr("名称 / 进度");Layout.fillWidth:true;color:win.muted;font.pixelSize:11}
      Label{text:qsTr("状态 / 速度");Layout.preferredWidth:88;color:win.muted;font.pixelSize:11}
      Label{text:qsTr("操作");Layout.preferredWidth:win.actionColumnWidth;horizontalAlignment:Text.AlignHCenter;color:win.muted;font.pixelSize:11}
     }
     Rectangle{Layout.fillWidth:true;Layout.topMargin:10;implicitHeight:1;color:win.edge}
     RowLayout {
      Layout.fillWidth:true;Layout.topMargin:6;Layout.bottomMargin:6;spacing:6
      Label{text:qsTr("已选 ")+win.selectedIds.length+qsTr(" 项");color:win.muted;Layout.fillWidth:true}
      Action{text:qsTr("全选");onClicked:win.selectAll()}
      Action{text:qsTr("暂停");enabled:win.canOperate&&win.eligible("pause");onClicked:win.batch("pause")}
      Action{text:qsTr("恢复");enabled:win.canOperate&&win.eligible("resume");onClicked:win.batch("resume")}
      Action{text:qsTr("重试");enabled:win.canOperate&&win.eligible("retry");onClicked:win.batch("retry")}
      Action{text:qsTr("移除…");enabled:win.canOperate&&win.eligible("remove");onClicked:win.askRemove()}
     }
     ListView {
      id:list;objectName:"taskList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:rows;boundsBehavior:Flickable.StopAtBounds
      ScrollBar.vertical:ScrollBar{}
      focus:true;interactive:false
      Keys.onPressed: event => {
       if(event.key===Qt.Key_A&&(event.modifiers&Qt.ControlModifier)){win.selectAll();event.accepted=true}
       else if(event.key===Qt.Key_Delete){win.askRemove();event.accepted=true}
       else if(event.key===Qt.Key_Escape){win.setSelection([],-1);event.accepted=true}
       else if(event.key===Qt.Key_C&&(event.modifiers&Qt.ControlModifier)){downloadService.copyLinks(win.selectedIds);event.accepted=true}
       else if(event.key===Qt.Key_Menu||(event.key===Qt.Key_F10&&(event.modifiers&Qt.ShiftModifier))){taskMenu.popup();event.accepted=true}
       else if(event.key===Qt.Key_Return&&win.selectedIds.length===1){detail.open();event.accepted=true}
       else if([Qt.Key_Up,Qt.Key_Down,Qt.Key_Home,Qt.Key_End].includes(event.key)&&rows.count){let ids=win.visibleIds(),i=ids.indexOf(win.selectedId);i=event.key===Qt.Key_Home?0:event.key===Qt.Key_End?ids.length-1:Math.max(0,Math.min(ids.length-1,i+(event.key===Qt.Key_Up?-1:1)));win.pick(ids[i],event.modifiers);list.positionViewAtIndex(i,ListView.Contain);event.accepted=true}
      }
      MouseArea {
       id:marquee;anchors.fill:parent;z:10;acceptedButtons:Qt.LeftButton|Qt.RightButton
       property real startX:0;property real startY:0;property real endX:0;property real endY:0
       property bool dragging:false;property int pressedId:-1;property var base:[];property int modifiers:0
       function updateSelection(){
        let lo=Math.min(startY,endY),hi=Math.max(startY,endY),ids=[];
        for(let i=0;i<rows.count;i++)if(i*94<hi&&(i+1)*94>lo)ids.push(rows.get(i).taskId)
        win.setSelection((modifiers&Qt.ControlModifier)?Array.from(new Set(base.concat(ids))):ids,ids.length?ids[ids.length-1]:-1)
       }
       onPressed: mouse => {
        const i=list.indexAt(mouse.x,mouse.y+list.contentY);pressedId=i<0?-1:rows.get(i).taskId
        if(mouse.button===Qt.LeftButton&&i>=0&&mouse.x>=width-win.actionColumnWidth-12){mouse.accepted=false;return}
        list.forceActiveFocus();modifiers=mouse.modifiers
        if(mouse.button===Qt.RightButton){const p=mapToItem(win.contentItem,mouse.x,mouse.y);win.context(pressedId,p.x,p.y);return}
        startX=endX=mouse.x;startY=endY=mouse.y+list.contentY;dragging=false;base=win.selectedIds.slice()
       }
       onPositionChanged: mouse => {if(!(pressedButtons&Qt.LeftButton))return;endX=mouse.x;endY=mouse.y+list.contentY;if(Math.abs(endX-startX)+Math.abs(endY-startY)>6)dragging=true;if(dragging)updateSelection()}
       onReleased: mouse => {if(mouse.button!==Qt.LeftButton)return;if(!dragging){if(pressedId>=0)win.pick(pressedId,modifiers);else win.setSelection([],-1)}dragging=false}
       onCanceled:dragging=false
       onDoubleClicked:mouse => {if(pressedId>=0){win.pick(pressedId,0);detail.open()}}
       onWheel:wheel=>{list.contentY=Math.max(0,Math.min(Math.max(0,list.contentHeight-list.height),list.contentY-wheel.angleDelta.y));wheel.accepted=true}
       Timer{interval:40;repeat:true;running:marquee.dragging;onTriggered:{let dy=marquee.mouseY<24?-12:marquee.mouseY>marquee.height-24?12:0;if(dy){list.contentY=Math.max(0,Math.min(Math.max(0,list.contentHeight-list.height),list.contentY+dy));marquee.endY=marquee.mouseY+list.contentY;marquee.updateSelection()}}}
      }
      Rectangle{z:11;visible:marquee.dragging;x:Math.min(marquee.startX,marquee.endX);y:Math.min(marquee.startY,marquee.endY)-list.contentY;width:Math.abs(marquee.endX-marquee.startX);height:Math.abs(marquee.endY-marquee.startY);color:"#22416daf";border.color:win.blue}

      delegate:ItemDelegate {
       id:row
       required property int taskId
       required property string fileName
       required property string taskData
       readonly property var task:JSON.parse(taskData)
       width:list.width;height:94;padding:12
       Accessible.name:fileName+"，"+win.status(task)
       onClicked:win.pick(taskId,0)
       onDoubleClicked:{win.pick(taskId,0);detail.open()}
       background:Rectangle{
        radius:8;color:win.selectedIds.includes(row.taskId)?"#e8f0fc":row.hovered?"#f5f7fb":"white";border.color:row.activeFocus?win.blue:"transparent"
        Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:1;color:win.edge;visible:!win.selectedIds.includes(row.taskId)}
       }
       contentItem:RowLayout{
        spacing:12
        Glyph{Layout.preferredWidth:24;Layout.preferredHeight:24}
        ColumnLayout{
         Layout.fillWidth:true;Layout.minimumWidth:0;spacing:6
         Label{text:row.fileName;Layout.fillWidth:true;elide:Text.ElideMiddle;color:win.ink;font.pixelSize:13;font.weight:Font.Medium;ToolTip.visible:row.hovered;ToolTip.text:row.fileName}
         Label{
          text:win.sizeText(row.task.bytes)+(row.task.total===null?qsTr(" / 大小未知"):" / "+win.sizeText(row.task.total)+(row.task.total>0?"   "+(100*row.task.bytes/row.task.total).toFixed(1)+"%":""))+(row.task.error?" · "+row.task.error.message:"")
          Layout.fillWidth:true;elide:Text.ElideRight;color:row.task.error?"#a63f45":win.muted;font.pixelSize:11
         }
         ProgressBar{
          Layout.fillWidth:true;Layout.preferredHeight:4
          from:0;to:Math.max(1,row.task.total||1);value:row.task.bytes
          indeterminate:row.task.total===null&&["downloading","fetching"].includes(row.task.status)
          background:Rectangle{color:"#e5ebf4";radius:2}
          contentItem:Item{
           clip:true
           Rectangle{
            height:4;radius:2
            width:row.task.total===null?parent.width*.25:parent.width*Math.min(1,row.task.bytes/Math.max(1,row.task.total))
            x:row.task.total===null?unknownAnim.position*parent.width:0
            color:row.task.status==="completed"?"#2b7e65":row.task.status==="paused"?win.muted:win.blue
            visible:row.task.total!==null||["downloading","fetching"].includes(row.task.status)
            QtObject{id:unknownAnim;property real position:0}
            NumberAnimation{target:unknownAnim;property:"position";from:-.25;to:1;duration:1500;loops:Animation.Infinite;running:row.task.total===null&&["downloading","fetching"].includes(row.task.status)}
           }
          }
         }
        }
        ColumnLayout{
         Layout.preferredWidth:88;spacing:4
         Label{text:win.status(row.task);color:row.task.status==="failed"?"#a63f45":row.task.status==="completed"?"#2b7e65":row.task.status==="downloading"?win.blue:win.ink;font.pixelSize:12}
         Label{text:row.task.status==="downloading"?win.sizeText(row.task.speed)+"/s":row.task.status==="queued"?qsTr("等待核心调度"):row.task.status==="completed"?qsTr("文件已保存"):"—";font.pixelSize:11;color:win.muted}
        }
        RowLayout{
         Layout.minimumWidth:win.actionColumnWidth;Layout.maximumWidth:win.actionColumnWidth;spacing:4
         Action{text:qsTr("打开文件");visible:win.category==="completed";enabled:win.canOperate;Layout.fillWidth:true;Accessible.name:text+" "+row.fileName;onClicked:win.openTask(row.task,false)}
         Action{text:qsTr("所在目录");visible:win.category==="completed";enabled:win.canOperate;Layout.fillWidth:true;Accessible.name:text+" "+row.fileName;onClicked:win.openTask(row.task,true)}
         Action{
          Layout.fillWidth:true;text:win.actionName(row.task)
          enabled:win.canOperate&&!row.task.pauseRequested&&row.task.status!=="removing"
          Accessible.name:text+" "+row.fileName;onClicked:win.act(row.task)
         }
        }
       }
      }
      Column {
       anchors.centerIn:parent;visible:rows.count===0;spacing:12
       Label{anchors.horizontalCenter:parent.horizontalCenter;text:!win.receivedList?qsTr("正在读取任务列表"):search.text?qsTr("没有匹配的任务"):win.category==="all"?qsTr("从第一个下载开始"):qsTr("此分类暂无任务");font.pixelSize:18;font.bold:true;color:win.ink}
       Label{anchors.horizontalCenter:parent.horizontalCenter;visible:win.receivedList;text:search.text?qsTr("试试其他关键词"):qsTr("新建任务后，可在这里查看真实进度");font.pixelSize:12;color:win.muted}
      }
     }
    }
    Rectangle {
     visible:win.selectedIds.length===1&&!!win.selected;Layout.fillWidth:true;Layout.preferredHeight:win.height<650?130:169;radius:10;color:win.soft;border.color:win.edge
     ColumnLayout{
      anchors.fill:parent;anchors.margins:14;spacing:8
      RowLayout{
       Layout.fillWidth:true
       Label{text:win.selected?.name||"";Layout.fillWidth:true;elide:Text.ElideMiddle;font.bold:true;font.pixelSize:12;color:win.ink}
       Action{text:qsTr("完整详情");onClicked:detail.open()}
       ToolButton{Accessible.name:qsTr("关闭任务详情");implicitWidth:28;implicitHeight:28;contentItem:Glyph{name:"close"} onClicked:win.setSelection([],-1)}
      }
      GridLayout{
       columns:2;Layout.fillWidth:true;columnSpacing:12;rowSpacing:6
       Label{text:qsTr("状态");font.pixelSize:11;color:win.muted}
       Label{text:win.selected?win.status(win.selected)+(win.selected.error?" · "+win.selected.error.message:""):"";Layout.fillWidth:true;elide:Text.ElideRight;font.pixelSize:11;color:win.ink}
       Label{text:qsTr("来源");font.pixelSize:11;color:win.muted}
       Label{text:win.selected?.url||"";Layout.fillWidth:true;elide:Text.ElideMiddle;font.pixelSize:11;color:win.ink}
       Label{text:qsTr("保存至");font.pixelSize:11;color:win.muted}
       Label{text:win.selected?.path||"";Layout.fillWidth:true;elide:Text.ElideMiddle;font.pixelSize:11;color:win.ink}
      }
      Item{Layout.fillHeight:true}
     }
    }
   }
  }
  Rectangle {
   Layout.fillWidth:true;Layout.preferredHeight:40;color:win.soft
   Rectangle{width:parent.width;height:1;color:win.edge}
   RowLayout{
    anchors.fill:parent;anchors.leftMargin:18;anchors.rightMargin:18;spacing:18
    Label{text:win.latest.length+qsTr(" 个任务");color:win.muted;font.pixelSize:11}
    Label{text:win.closingService?qsTr("正在安全关闭…"):!downloadService.available?qsTr("服务未连接"):"";color:"#a63f45";font.pixelSize:11}
    Item{Layout.fillWidth:true}
    Label{text:downloadService.downloadPath;Layout.maximumWidth:win.width*.45;elide:Text.ElideMiddle;color:win.muted;font.pixelSize:11}
   }
  }
 }
 Sheet {
  id:composer;title:qsTr("新建下载任务");width:Math.min(520,win.width-48)
  onClosed:newButton.forceActiveFocus()
  contentItem:ColumnLayout{
   spacing:12
   Action{text:qsTr("打开本地种子…");onClicked:downloadService.openTorrent(taskDirectory.text)}
   Label{text:qsTr("HTTP / HTTPS / Magnet 链接，每行一个");color:win.muted;font.pixelSize:12}
   ScrollView{
    Layout.fillWidth:true;Layout.preferredHeight:130
    background:Rectangle{radius:8;color:win.soft;border.color:entry.activeFocus?win.blue:win.edge}
    TextArea{id:entry;Accessible.name:qsTr("下载链接");padding:10;placeholderText:"https://";color:win.ink;background:null;wrapMode:TextEdit.Wrap;selectByMouse:true}
   }
   Label{text:qsTr("可在链接后附上 | SHA-256，用于完整性校验。");color:win.muted;font.pixelSize:11}
   RowLayout{Layout.fillWidth:true
    TextField{id:taskDirectory;text:downloadService.downloadPath;Layout.fillWidth:true;placeholderText:qsTr("本次任务保存目录")}
    Action{text:qsTr("浏览");onClicked:{let p=downloadService.chooseDirectory();if(p)taskDirectory.text=p}}
   }
   Label{visible:win.errorText!=="";text:win.errorText;Layout.fillWidth:true;wrapMode:Text.Wrap;color:"#a63f45";font.pixelSize:12}
   RowLayout{
    Layout.alignment:Qt.AlignRight
    Action{text:qsTr("取消");enabled:!win.submitting;onClicked:composer.close()}
    Action{text:win.submitting?qsTr("正在提交"):qsTr("添加到队列");primary:true;enabled:!win.submitting&&downloadService.available&&entry.text.trim().length>0;onClicked:{win.submitting=true;win.errorText="";downloadService.create(entry.text,taskDirectory.text)}}
   }
  }
 }
 Sheet {
  id:detail;title:qsTr("任务详情");width:Math.min(600,win.width-48);height:Math.min(490,win.height-50)
  contentItem:ScrollView{
   TextArea{
    Accessible.name:qsTr("完整任务信息");readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;color:win.ink
    text:{
     const t=win.selected;if(!t)return ""
     return t.name+"\n"+win.status(t)+qsTr("\n\n已接收 ")+win.sizeText(t.bytes)+" / "+win.sizeText(t.total)+qsTr("\n\n保存至\n")+t.path+qsTr("\n\n来源\n")+t.url+(t.error?qsTr("\n\n错误原因\n")+t.error.message:"")+(t.recovery?.message?qsTr("\n\n恢复说明\n")+t.recovery.message:"")+(t.digest?"\n\nSHA-256\n"+t.digest:"")
    }
   }
  }
  footer:ColumnLayout{
   Label{text:qsTr("仅可打开已完成的文件。");font.pixelSize:11;color:win.muted;Layout.margins:12}
   RowLayout{
    Layout.alignment:Qt.AlignRight;Layout.rightMargin:18;Layout.bottomMargin:16
    Action{text:qsTr("打开文件");enabled:!!win.selected&&win.selected.status==="completed";onClicked:win.openTask(win.selected,false)}
    Action{text:qsTr("所在目录");enabled:!!win.selected;onClicked:win.openTask(win.selected,true)}
    Action{text:qsTr("关闭");onClicked:detail.close()}
   }
  }
 }
 Sheet {
  id:settings;title:qsTr("设置");width:Math.min(520,win.width-48)
  contentItem:ColumnLayout{
   spacing:8
   RowLayout{
    Label{text:"Eego "+Qt.application.version;color:win.muted}
    Action{text:qsTr("检查更新");onClicked:appUpdater.check(true)}
   }
   Label{text:qsTr("最多同时下载任务");font.bold:true;color:win.ink}
   RowLayout{
    ComboBox{id:concurrencyChoice;objectName:"concurrencyChoice";Accessible.name:qsTr("最多同时下载任务");model:Array.from({length:8},(_,i)=>String(i+1));currentIndex:downloadService.maxActiveTasks-1;enabled:downloadService.available&&!downloadService.settingsPending
     popup:Popup{
      y:concurrencyChoice.height;width:concurrencyChoice.width;padding:4;height:Math.min(202,win.height-32)
      onOpened:limits.positionViewAtIndex(concurrencyChoice.currentIndex,ListView.Contain)
      background:Rectangle{radius:9;color:"white";border.color:win.edge}
      contentItem:ListView{id:limits;clip:true;model:concurrencyChoice.delegateModel;currentIndex:concurrencyChoice.highlightedIndex;highlightMoveDuration:0
       ScrollBar.vertical:ScrollBar{policy:ScrollBar.AlwaysOn;interactive:true}
      }
     }
    }
    Action{text:downloadService.settingsPending?qsTr("保存中…"):qsTr("应用");enabled:downloadService.available&&!downloadService.settingsPending;onClicked:{win.errorText="";downloadService.setMaxActiveTasks(concurrencyChoice.currentIndex+1)}}
    Label{text:qsTr("当前上限：")+downloadService.maxActiveTasks;color:win.muted}
   }
   Label{text:qsTr("HTTP 与 BT 合计最多同时下载 8 个任务，默认 1 个。边下载边工作时，建议先开 1～3 个。如果打字或切换窗口变卡，可以减少任务数。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.muted;font.pixelSize:12}
   Label{text:qsTr("调低上限后，正在下载的任务会继续，排队任务等名额空出后再开始。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.muted;font.pixelSize:12}
   Label{text:downloadService.settingsWarning;visible:text.length>0;Layout.fillWidth:true;wrapMode:Text.Wrap;color:"#a63f45"}
   Label{text:qsTr("下载位置");font.bold:true;color:win.ink}
   TextArea{text:downloadService.downloadPath;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;Layout.fillWidth:true;color:win.muted;background:Rectangle{color:win.soft;radius:8}}
   Label{text:qsTr("更改默认目录后，新任务会使用新位置。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.muted;font.pixelSize:12}
   Label{text:win.errorText;visible:text.length>0;Layout.fillWidth:true;wrapMode:Text.Wrap;color:"#a63f45"}
   Action{text:qsTr("更改默认目录");onClicked:{win.errorText="";let p=downloadService.chooseDirectory();if(p)downloadService.setDefaultDirectory(p)}}
   Rectangle{Layout.fillWidth:true;implicitHeight:1;color:win.edge}
   RowLayout{Layout.fillWidth:true
    ColumnLayout{Layout.fillWidth:true
     Label{text:qsTr("支持作者");textFormat:Text.PlainText;font.bold:true;color:win.ink}
     Label{text:qsTr("如果你喜欢鹅鹅下载，可以扫码支持。");textFormat:Text.PlainText;wrapMode:Text.Wrap;Layout.fillWidth:true;color:win.muted}
     Action{text:qsTr("放大收款码");onClicked:paymentPreview.open()}
    }
    Rectangle{implicitWidth:118;implicitHeight:118;radius:10;color:"white";border.color:win.edge
     Image{id:supportImage;anchors.fill:parent;anchors.margins:6;source:"qrc:/qt/qml/HandmadeBT/assets/author-payment.jpg";fillMode:Image.PreserveAspectFit}
     TapHandler{onTapped:paymentPreview.open()}
    }
   }
  }
 }
 Sheet{
  id:paymentPreview;title:qsTr("支持作者");width:Math.min(510,win.width-48);height:Math.min(690,win.height-36)
  contentItem:Image{source:supportImage.source;fillMode:Image.PreserveAspectFit}
 }
 Sheet{
  id:closeChoice;title:qsTr("关闭鹅鹅下载");width:Math.min(420,win.width-48)
  contentItem:ColumnLayout{
   Label{text:qsTr("收进通知区域后，下载会继续。完全退出会保存任务并停止下载。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.muted}
   RowLayout{Layout.alignment:Qt.AlignRight
    Action{text:qsTr("取消");onClicked:closeChoice.close()}
    Action{text:qsTr("完全退出");onClicked:{closeChoice.close();win.exitApplication()}}
    Action{text:qsTr("收进通知区域");enabled:closeChoice.opened&&downloadService.trayAvailable();primary:true;onClicked:{closeChoice.close();win.hide()}}
   }
  }
 }
 component TaskMenuItem: MenuItem{
  id:entryItem;implicitHeight:33
  background:Rectangle{radius:7;color:entryItem.highlighted?"#e8f0fb":"transparent"}
  contentItem:Text{text:entryItem.text;font:entryItem.font;color:entryItem.enabled?win.ink:"#99a5b6";verticalAlignment:Text.AlignVCenter}
 }
 Menu {
  padding:6;popupType:Popup.Item
  background:Rectangle{implicitWidth:190;radius:12;border.color:win.edge;color:"white"}
  id:taskMenu;objectName:"taskMenu";parent:win.contentItem
  TaskMenuItem{text:qsTr("暂停");enabled:win.canOperate&&win.eligible("pause");onTriggered:win.batch("pause")}
  TaskMenuItem{text:qsTr("恢复");enabled:win.canOperate&&win.eligible("resume");onTriggered:win.batch("resume")}
  TaskMenuItem{text:qsTr("重试");enabled:win.canOperate&&win.eligible("retry");onTriggered:win.batch("retry")}
  MenuSeparator{}
  TaskMenuItem{text:qsTr("移除…");enabled:win.canOperate&&win.eligible("remove");onTriggered:win.askRemove()}
  TaskMenuItem{text:qsTr("打开文件");enabled:win.selectedIds.length===1&&!!win.selected&&win.selected.status==="completed";onTriggered:win.openTask(win.selected,false)}
  TaskMenuItem{text:qsTr("打开所在文件夹");enabled:win.selectedIds.length===1;onTriggered:win.openTask(win.selected,true)}
  TaskMenuItem{text:qsTr("选择 / 查看文件");enabled:win.selectedIds.length===1&&!!win.selected&&win.selected.url.startsWith("magnet:?")&&win.selected.files.length>0;onTriggered:win.showFiles(win.selected)}
  TaskMenuItem{text:qsTr("复制链接");enabled:win.selectedIds.length>0;onTriggered:downloadService.copyLinks(win.selectedIds)}
  MenuSeparator{}
  TaskMenuItem{text:qsTr("全选当前列表");enabled:rows.count>0;onTriggered:win.selectAll()}
 }
 Sheet {
  id:removeDialog;objectName:"removeDialog";title:qsTr("移除选中的 ")+win.removeTargets.length+qsTr(" 个任务？");width:Math.min(610,win.width-48)
  onOpened:cancelRemove.forceActiveFocus()
  contentItem:ColumnLayout {
   spacing:12
   Label{text:qsTr("运行中的任务会先停止并保存。仅影响以下任务，不删除整个文件夹。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.ink}
   ScrollView{Layout.fillWidth:true;Layout.preferredHeight:110;TextArea{readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;text:win.removeTargets.map(t=>"#"+t.id+" "+t.name+"\n"+t.path).join("\n");color:win.muted}}
   Label{text:qsTr("仅移除：保留下载文件、临时文件、恢复记录和缓存。\n同时删除：仅将归属已登记且身份未变的文件及缓存送入回收站。未登记、被修改或额外文件会阻止删除；无法可靠回收时停止，无永久删除选项。空目录保留。");Layout.fillWidth:true;wrapMode:Text.Wrap;color:win.muted}
   Action{text:qsTr("仅移除任务，保留文件");Layout.fillWidth:true;enabled:win.canOperate;onClicked:{downloadService.batch("remove",win.removeTargets.map(t=>t.id),false);removeDialog.close()}}
   Action{text:qsTr("同时将下载文件送入回收站");Layout.fillWidth:true;enabled:win.canOperate;onClicked:{downloadService.batch("remove",win.removeTargets.map(t=>t.id),true);removeDialog.close()}}
   Action{id:cancelRemove;text:qsTr("取消");Layout.alignment:Qt.AlignRight;onClicked:removeDialog.close();Keys.onReturnPressed:removeDialog.close();Keys.onEnterPressed:removeDialog.close()}
  }
 }
 ListModel{id:fileChoices}
 Sheet {
  id:selection;objectName:"fileSelection";title:win.viewFilesOnly?qsTr("查看下载文件"):qsTr("选择需要下载的文件");width:Math.min(760,win.width-48);height:Math.min(530,win.height-50)
  background:Rectangle{radius:16;color:"#f2f5fa";border.color:win.edge}
  property int taskId: -1
  readonly property var kinds: ["video","subtitle","audio","image","other"]
  readonly property var kindLabels: ({video:qsTr("视频"),subtitle:qsTr("字幕"),audio:qsTr("音频"),image:qsTr("图片"),other:qsTr("其他")})
  property var totals: ({count:0,bytes:0,groups:{},folders:{}})
  property var folders: []
  property bool folderPanel: false
  property int sortMode: 0
  property var order: []
  function sortFiles(mode){
   const indices=[], sizes=[]
   for(let i=0;i<fileChoices.count;++i){indices.push(i);sizes.push(fileChoices.get(i).bytes)}
   if(mode!==0)indices.sort((a,b)=>(mode===1 ? sizes[b]-sizes[a] : sizes[a]-sizes[b]) || a-b)
   sortMode=mode
   order=indices
   fileList.positionViewAtBeginning()
  }
  function fileKind(path){
   const match=/\.([^.\\/]+)$/.exec(path.toLowerCase())
   const ext=match ? match[1] : ""
   if(/^(mp4|mkv|avi|mov|m4v|webm|wmv|flv|ts|m2ts|mpeg|mpg|rmvb|vob|3gp)$/.test(ext))return "video"
   if(/^(srt|ass|ssa|vtt|sub|idx|sup|smi|lrc)$/.test(ext))return "subtitle"
   if(/^(mp3|flac|wav|aac|m4a|ogg|opus|wma|ape|aiff|alac|ac3|dts)$/.test(ext))return "audio"
   if(/^(jpe?g|png|gif|webp|bmp|tiff?|svg|avif|heic|heif|ico)$/.test(ext))return "image"
   return "other"
  }
  function recount(){
   const result={count:0,bytes:0,groups:{},folders:Object.create(null)}
   for(let i=0;i<fileChoices.count;++i){
    const f=fileChoices.get(i)
    const group=result.groups[f.kind] || (result.groups[f.kind]={count:0,chosen:0})
    ++group.count
    const path=f.normalizedPath
    for(let end=path.indexOf("/");end>=0;end=path.indexOf("/",end+1)){
     if(end===0)continue
     const key=path.slice(0,end)
     const folder=result.folders[key] || (result.folders[key]={count:0,chosen:0})
     ++folder.count
     if(f.chosen)++folder.chosen
    }
    if(f.chosen){++group.chosen;++result.count;result.bytes+=f.bytes}
   }
   totals=result
  }
  function applyChoices(decide){
   for(let i=0;i<fileChoices.count;++i){
    const f=fileChoices.get(i), chosen=decide(f,i)
    if(chosen!==f.chosen)fileChoices.setProperty(i,"chosen",chosen)
   }
   recount()
  }
  function state(chosen,total){
   return chosen===0 ? Qt.Unchecked : chosen===total ? Qt.Checked : Qt.PartiallyChecked
  }
  component Mark: Rectangle{
   required property int stateValue
   implicitWidth:19;implicitHeight:19;radius:6
   color:stateValue===Qt.Unchecked?"white":win.blue
   border.color:stateValue===Qt.Unchecked?"#a9bbce":win.blue
   Text{anchors.centerIn:parent;text:parent.stateValue===Qt.PartiallyChecked?"−":parent.stateValue===Qt.Checked?"✓":"";color:"white";font.bold:true;font.pixelSize:14}
  }
  component GroupCheck: CheckBox {
   id:groupBox
   indicator:Mark{x:groupBox.leftPadding;y:(groupBox.height-height)/2;stateValue:groupBox.checkState}
   background:Rectangle{radius:9;color:groupBox.hovered?"#e4ebf5":"transparent";border.color:groupBox.activeFocus?win.blue:"transparent"}
   tristate:true
   nextCheckState:function(){return checkState===Qt.Checked ? Qt.Unchecked : Qt.Checked}
  }
  contentItem:ColumnLayout{
   Label{text:win.viewFilesOnly?qsTr("双击已完成且已选择的文件可打开。"):qsTr("未选文件不会下载；取消保留待选择任务。");color:win.muted}
   RowLayout{
    GroupCheck{text:qsTr("全选");visible:!win.viewFilesOnly;enabled:fileChoices.count>0
     checkState:selection.state(selection.totals.count,fileChoices.count)
     onClicked:{const pick=selection.totals.count<fileChoices.count;selection.applyChoices(()=>pick)}
    }
    Action{text:qsTr("反选");visible:!win.viewFilesOnly;enabled:fileChoices.count>0;onClicked:selection.applyChoices(f=>!f.chosen)}
    Label{text:qsTr("已选 ")+selection.totals.count+" / "+fileChoices.count+qsTr(" 个文件，共 ")+win.sizeText(selection.totals.bytes);color:win.ink}
   }
   RowLayout{visible:!win.viewFilesOnly
    Repeater{model:selection.kinds
     GroupCheck{required property string modelData
      readonly property var group:selection.totals.groups[modelData]
      text:selection.kindLabels[modelData];visible:!!group;checkState:group ? selection.state(group.chosen,group.count) : Qt.Unchecked
      onClicked:{const kind=modelData,pick=group.chosen<group.count;selection.applyChoices(f=>f.kind===kind ? pick : f.chosen)}
     }
    }
   }
   RowLayout{
    Action{text:selection.folderPanel?qsTr("收起文件夹"):qsTr("按文件夹选择");visible:!win.viewFilesOnly&&selection.folders.length>0;onClicked:selection.folderPanel=!selection.folderPanel}
    Item{Layout.fillWidth:true}
    Label{text:qsTr("文件排序");color:win.muted}
    ComboBox{id:sortBox;objectName:"fileSort";model:[qsTr("原始顺序"),qsTr("大小：从大到小"),qsTr("大小：从小到大")];currentIndex:selection.sortMode;onActivated:index=>selection.sortFiles(index)
     background:Rectangle{implicitWidth:145;implicitHeight:34;radius:10;color:"white";border.color:sortBox.activeFocus?win.blue:win.edge}
     indicator:Text{x:sortBox.width-width-10;y:(sortBox.height-height)/2;text:"⌄";color:win.muted}
    }
   }
   RowLayout{Layout.fillWidth:true;Layout.fillHeight:true
    ListView{visible:selection.folderPanel&&!win.viewFilesOnly;Layout.preferredWidth:Math.min(220,selection.width*0.3);Layout.fillHeight:true;clip:true;model:selection.folders
     ScrollBar.vertical:ScrollBar{}
     delegate:GroupCheck{required property string modelData
      readonly property var group:selection.totals.folders[modelData]
      text:modelData;width:ListView.view.width-10
      contentItem:Text{text:parent.text;leftPadding:parent.indicator.width+parent.spacing;verticalAlignment:Text.AlignVCenter;elide:Text.ElideMiddle;color:win.ink;font:parent.font}
      ToolTip.visible:hovered;ToolTip.text:modelData
      checkState:group ? selection.state(group.chosen,group.count) : Qt.Unchecked
      onClicked:{const prefix=modelData+"/", pick=group.chosen<group.count;selection.applyChoices(f=>f.normalizedPath.startsWith(prefix) ? pick : f.chosen)}
     }
    }
   ListView{id:fileList;objectName:"sortedFiles";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:4;model:selection.order
    ScrollBar.vertical:ScrollBar{}
    delegate:RowLayout{id:fileRow;required property int modelData
     readonly property var file:fileChoices.get(modelData)
     width:ListView.view.width-12
     CheckDelegate{id:fileCheck;Layout.fillWidth:true;Layout.minimumWidth:0;text:fileRow.file.path;checked:fileRow.file.chosen
      leftPadding:36;rightPadding:8
      indicator:Mark{x:9;y:(fileCheck.height-height)/2;stateValue:fileCheck.checkState}
      contentItem:Text{text:fileCheck.text;font:fileCheck.font;color:win.ink;verticalAlignment:Text.AlignVCenter;elide:Text.ElideMiddle}
      background:Rectangle{implicitHeight:38;radius:9;color:fileCheck.checked?"#e3eefc":fileCheck.hovered?"#edf2fa":"white";border.color:fileCheck.activeFocus?win.blue:"transparent"}
      nextCheckState:function(){return win.viewFilesOnly ? checkState : (checked ? Qt.Unchecked : Qt.Checked)}
      onClicked:{if(!win.viewFilesOnly){fileChoices.setProperty(fileRow.modelData,"chosen",checked);selection.recount()}}
      onDoubleClicked:{if(win.viewFilesOnly&&fileRow.file.chosen)downloadService.openPath(selection.taskId,false,fileRow.file.fileIndex)}
      ToolTip.visible:hovered;ToolTip.text:text
     }
     Label{text:win.sizeText(fileRow.file.bytes);color:win.muted}
     Action{text:qsTr("仅选此项");visible:!win.viewFilesOnly;onClicked:selection.applyChoices((f,i)=>i===fileRow.modelData)}
    }
   }
   }
   Label{text:win.errorText;visible:text.length>0;wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#a63f45"}
   RowLayout{
    Action{text:qsTr("取消");onClicked:selection.close()}
    Action{text:qsTr("下载所选文件");visible:!win.viewFilesOnly;enabled:selection.totals.count>0&&win.canOperate;primary:true;onClicked:{let indexes=[];for(let i=0;i<fileChoices.count;i++)if(fileChoices.get(i).chosen)indexes.push(fileChoices.get(i).fileIndex);downloadService.selectFiles(selection.taskId,indexes);selection.close()}}
   }
  }
 }

}
