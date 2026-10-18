// Run the embedded page's actual JS against a small DOM and HTTP test fixture.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.join(__dirname,'../main');
const script=fs.readFileSync(path.join(root,'portal.js'),'utf8');
const groups=JSON.parse(fs.readFileSync(path.join(root,'cities.json'),'utf8'));
const guizhou=groups.find(group=>group.name==='贵州省');
const guiyang=guizhou.cities.find(city=>city.name==='贵阳');
class Element {
  constructor(select=false){this.select=select;this.options=[];this._value='';this.dataset={};this.textContent='';this.checked=false;this.disabled=false;this.hidden=false;}
  set value(value){this._value=this.select&&!this.options.some(item=>item.value===value)?'':value;}
  get value(){return this._value;}
  get selectedOptions(){return this.options.filter(item=>item.value===this._value);}
  append(item){this.options.push(item);if(this.options.length===1)this._value=item.value;}
  replaceChildren(...items){this.options=[];this._value='';items.forEach(item=>this.append(item));}
}
async function setup(overrides={},networks=[{ssid:'Home',rssi:-44,secure:true},{ssid:'客厅网络',rssi:-60,secure:true},{ssid:'Different network',rssi:-72,secure:true}],beforeScanReply){
  const elements=new Map(),posts=[],urls=[];
  const element=id=>{if(!elements.has(id))elements.set(id,new Element(['ssid','province','city'].includes(id)));return elements.get(id);};
  const state=new URLSearchParams({ssid:'Home',city:'贵阳',brightness:'25',day:'06:00',dusk:'17:00',night:'19:00',has_password:'1',has_key:'1',online:'1',synced:'1',...overrides});
  class FormDataMock extends Map{constructor(){super(['ssid','password','city','api_key','brightness','day','dusk','night'].map(key=>[key,element(key).value]));if(element('open_wifi').checked)this.set('open_wifi','1');}}
  const context={URLSearchParams,TextEncoder,FormData:FormDataMock,setTimeout,
    Option:function(text,value){return {text,value,dataset:{}};},document:{getElementById:element},
    fetch:async(url,options)=>{
      if(url==='/api/state')return {ok:true,text:async()=>state.toString()};
      if(url==='/api/cities')return {ok:true,json:async()=>groups};
      if(url.startsWith('/api/wifi')){
        if(beforeScanReply)await beforeScanReply(element);
        return {ok:true,json:async()=>({status:'ready',networks})};
      }
      urls.push(url);posts.push(options);return {ok:true,text:async()=>url==='/api/preview'?'正在预览':'已保存'};
    }};
  vm.runInNewContext(script,context);
  await new Promise(resolve=>setImmediate(resolve));
  return {element,posts,urls,submit:()=>element('setup').onsubmit({preventDefault(){}})};
}
(async()=>{
  assert.equal(groups.length,31);
  const ids=groups.flatMap(group=>group.cities.map(city=>city.id));
  assert.equal(ids.length,371);assert.equal(new Set(ids).size,ids.length);

  let ui=await setup();
  assert.equal(ui.element('province').value,'贵州省');assert.equal(ui.element('city').value,guiyang.id);
  assert.equal(ui.element('password').value,'');assert.equal(ui.element('api_key').value,'');
  await ui.submit();assert.equal(ui.posts.length,1);assert.equal(ui.posts[0].headers['X-Case-Setup'],'1');
  assert.equal(new URLSearchParams(ui.posts[0].body).get('city'),guiyang.id);

  ui=await setup();ui.element('ssid').value='客厅网络';ui.element('password').value='12345678';await ui.submit();
  assert.equal(new URLSearchParams(ui.posts[0].body).get('ssid'),'客厅网络');

  ui=await setup();ui.element('ssid').value='Different network';await ui.submit();assert.equal(ui.posts.length,0);

  ui=await setup();ui.element('ssid').value='Different network';ui.element('open_wifi').checked=true;await ui.submit();assert.equal(ui.posts.length,1);

  ui=await setup();ui.element('manual_wifi').checked=true;ui.element('manual_wifi').onchange();ui.element('manual_ssid').value='中'.repeat(11);await ui.submit();assert.equal(ui.posts.length,0);

  ui=await setup();ui.element('day').value='20:00';await ui.submit();assert.equal(ui.posts.length,0);

  ui=await setup({ssid:'',city:'',has_password:'0'});ui.element('ssid').value='Home';ui.element('password').value='12345678';await ui.submit();assert.equal(ui.posts.length,0,'Initial configuration requires a city selection');

  ui=await setup({city:'AnOldCustomCity'});assert.equal(ui.element('city').value,'AnOldCustomCity');await ui.submit();assert.equal(ui.posts.length,1);

  ui=await setup({ssid:'Saved hidden network'},[]);assert.equal(ui.element('ssid').value,'Saved hidden network');await ui.submit();assert.equal(ui.posts.length,1);

  ui=await setup({},[{ssid:'Home',rssi:-50,secure:true},{ssid:'Home',rssi:-70,secure:true}]);assert.equal(ui.element('ssid').options.filter(item=>item.value==='Home').length,1);

  ui=await setup();ui.element('province').value='北京市';ui.element('province').onchange();assert.equal(ui.element('city').value,groups.find(g=>g.name==='北京市').cities[0].id);

  ui=await setup();ui.element('password').value='short';await ui.submit();assert.equal(ui.posts.length,0,'A short replacement password is rejected');

  ui=await setup({ssid:'Saved open network',has_password:'0'},[]);
  assert.equal(ui.element('open_wifi').checked,true);await ui.submit();assert.equal(ui.posts.length,1,'An unseen saved open network remains editable without re-entering credentials');

  ui=await setup({},undefined,async element=>{
    await new Promise(resolve=>setImmediate(resolve));
    element('ssid').append({text:'During scan',value:'During scan',dataset:{secure:'true'}});
    element('ssid').value='During scan';
  });
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(ui.element('ssid').value,'During scan','Scan results must preserve the latest selection');

  ui=await setup({ssid:'Old open network',has_password:'0'},[]);
  ui.element('manual_wifi').checked=true;ui.element('manual_wifi').onchange();
  ui.element('manual_ssid').value='New protected network';ui.element('manual_ssid').oninput();
  assert.equal(ui.element('open_wifi').checked,false,'Changing a manually entered network resets the open-network choice');

  ui=await setup();ui.element('setup').dataset.updating='1';await ui.submit();
  assert.equal(ui.posts.length,0,'Settings must not submit during a firmware upload');
  ui=await setup();assert.equal(ui.element('auto_dim').checked,true);assert.equal(ui.element('night_brightness').value,'8');
  ui=await setup({brightness:'4'});assert.equal(ui.element('night_brightness').value,'4','New lighting defaults must not brighten a dim installation');
  ui=await setup({auto_dim:'0',night_brightness:'12'});
  assert.equal(ui.element('nightBrightnessRow').hidden,true);await ui.submit();
  assert.equal(new URLSearchParams(ui.posts[0].body).get('auto_dim'),'0');
  assert.equal(new URLSearchParams(ui.posts[0].body).get('night_brightness'),'12');
  ui=await setup({auto_dim:'1',night_brightness:'7'});
  assert.equal(ui.element('nightBrightnessRow').hidden,false);
  ui.element('night_brightness').value='11';ui.element('night_brightness').oninput();
  assert.equal(ui.element('nightBrightnessValue').textContent,'11');await ui.submit();
  assert.equal(new URLSearchParams(ui.posts[0].body).get('night_brightness'),'11');
  for(const value of ['0','101','-1','abc','']) {
    ui=await setup();ui.element('night_brightness').value=value;await ui.submit();
    assert.equal(ui.posts.length,0,'Reject invalid night brightness: '+value);
  }
  console.log('Setup checks passed: city dataset + 16 existing scenarios + lighting defaults, disable/save and invalid values.');
})().catch(error=>{console.error(error);process.exitCode=1;});
