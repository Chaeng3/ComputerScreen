const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const script=fs.readFileSync(path.join(__dirname,'../main/ota.js'),'utf8');
function file({name='case_display-1.1.0-ota.bin',version='1.1.0-ota',project='case_display',chip=9,size=4096,magic=0xe9}={}) {
  const b=Buffer.alloc(320);b[0]=magic;b[1]=3;b.writeUInt16LE(chip,12);b[23]=1;b.writeUInt32LE(0xabcd5432,32);b.write(version,48);b.write(project,80);
  return {name,size,slice:()=>({arrayBuffer:async()=>Uint8Array.from(b.subarray(0,288)).buffer})};
}
async function setup({mode='success',info={version:'1.1.0-ota',max_size:4194304,validating:false,restarting:false}}={}) {
  const elements=new Map(),requests=[];
  const el=id=>{if(!elements.has(id))elements.set(id,{dataset:{},files:[],disabled:false,textContent:'',hidden:true,value:0});return elements.get(id);};
  class XHR {
    constructor(){this.upload={};this.headers={};}
    open(method,url){this.method=method;this.url=url;}
    setRequestHeader(key,value){this.headers[key]=value;}
    send(body){requests.push({url:this.url,method:this.method,headers:this.headers,body});queueMicrotask(()=>{
      this.upload.onprogress({lengthComputable:true,loaded:body.size,total:body.size});
      if(mode==='network')return this.onerror();
      if(mode==='timeout')return this.ontimeout();
      this.status=mode==='reject'?400:200;this.responseText=mode==='reject'?'固件完整性校验失败':'固件校验通过，正在重启';this.onload();
    });}
  }
  vm.runInNewContext(script,{document:{getElementById:el},fetch:async()=>({ok:true,json:async()=>info}),XMLHttpRequest:XHR});
  await new Promise(resolve=>setImmediate(resolve));
  return {el,requests,upgrade:()=>el('upgrade').onclick()};
}
(async()=>{
  let ui=await setup();await ui.upgrade();assert.equal(ui.requests.length,0,'No file must not upload');
  for(const config of [{name:'wrong.txt'},{size:4194305},{size:12},{chip:2},{project:'another_project'},{version:'1.0'},{magic:0}]) {
    ui=await setup();ui.el('firmwareFile').files=[file(config)];await ui.upgrade();assert.equal(ui.requests.length,0,'Wrong file must be rejected locally');assert.equal(ui.el('setup').dataset.updating,undefined);
  }
  ui=await setup();const valid=file();ui.el('firmwareFile').files=[valid];await ui.upgrade();
  assert.equal(ui.requests.length,1);assert.equal(ui.requests[0].url,'/api/update');assert.equal(ui.requests[0].body,valid);
  assert.equal(ui.requests[0].headers['X-Case-Setup'],'1');assert.equal(ui.requests[0].headers['Content-Type'],'application/octet-stream');
  assert.equal(ui.el('upgradeProgress').value,100);assert.equal(ui.el('save').disabled,true);assert.equal(ui.el('setup').dataset.updating,'1');
  await ui.upgrade();assert.equal(ui.requests.length,1,'Double click must not start a second upload');
  for(const mode of ['reject','network','timeout']) {
    ui=await setup({mode});ui.el('firmwareFile').files=[file()];await ui.upgrade();
    assert.equal(ui.el('save').disabled,false);assert.equal(ui.el('upgrade').disabled,false);assert.equal(ui.el('setup').dataset.updating,undefined);
    assert.ok(ui.el('upgradeStatus').textContent.length>0);
  }
  ui=await setup();ui.el('setup').dataset.saving='1';ui.el('firmwareFile').files=[file()];await ui.upgrade();assert.equal(ui.requests.length,0);
  ui=await setup({info:{version:'1.1.0-ota',max_size:4194304,validating:true}});assert.equal(ui.el('upgrade').disabled,true);
  console.log('OTA page checks passed: file screening, upload/progress, retries, duplicate clicks and settings interlocks.');
})().catch(error=>{console.error(error);process.exitCode=1;});
