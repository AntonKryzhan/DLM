use std::collections::{HashSet,VecDeque};
#[derive(Clone,Debug,PartialEq,Eq)]
pub enum Error { Invalid(&'static str),UnknownWithinBudget }
impl std::fmt::Display for Error{fn fmt(&self,f:&mut std::fmt::Formatter<'_>)->std::fmt::Result{match self{Self::Invalid(s)=>write!(f,"Invalid: {s}"),Self::UnknownWithinBudget=>write!(f,"UnknownWithinBudget")}}}
impl std::error::Error for Error{}
type Result<T>=std::result::Result<T,Error>;
fn need(value:bool,text:&'static str)->Result<()>{if value{Ok(())}else{Err(Error::Invalid(text))}}
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Requirements{pub required:u32,pub forbidden:u32,pub preferred:u32}
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Policy{pub delivery:u32,pub latency:u32,pub loss:u32,pub throughput:u32,pub cost:u32,pub max_age:u32,pub min_samples:u32,pub min_hold:u32,pub hysteresis:u32}
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Observation{pub id:u64,pub at:u64,pub rtt:u64,pub goodput:u64,pub samples:u64,pub loss:u32,pub profile:u32,pub properties:u32,pub cost:u32,pub ready:bool}
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Choice{pub id:u64,pub score:u64,pub profile:u32,pub properties:u32,pub index:u32,pub status:u32}
pub fn properties(profile:u32,delivery:u32)->u32{let raw=match profile{1=>179,2=>183,3=>208,4=>191,5=>253,6=>244,_=>return 0};match delivery{0=>raw,1=>(raw&440)|7,2=>(raw&440)|69,_=>0}}
pub fn valid_policy(p:Policy)->bool{p.delivery<=2 && [p.latency,p.loss,p.throughput,p.cost].iter().all(|v|*v<=1000000) && p.latency|p.loss|p.throughput|p.cost!=0 && (1..=3600000).contains(&p.max_age) && (1..=1000000).contains(&p.min_samples) && p.min_hold<=3600000 && p.hysteresis<=100}
pub fn valid_requirements(r:Requirements)->bool{(r.required|r.forbidden|r.preferred)&!511==0 && r.required&r.forbidden==0 && r.preferred&r.forbidden==0 && r.required&66!=66}
pub fn eligible(o:Observation,p:Policy,r:Requirements,now:u64)->bool{o.ready && o.id!=0 && properties(o.profile,p.delivery)!=0 && o.properties==properties(o.profile,p.delivery) && o.cost<=1000000 && o.rtt<=60000000000 && o.goodput<=1000000000000 && o.loss<=1000000 && o.samples>=p.min_samples as u64 && o.at<=now && now-o.at<=p.max_age as u64 && r.required&o.properties==r.required && r.forbidden&o.properties==0}
pub fn score(o:Observation,p:Policy)->u64{if !valid_policy(p)||o.rtt>60000000000||o.goodput>1000000000000||o.loss>1000000||o.cost>1000000{return u64::MAX}(o.rtt/1000).min(1000000)*p.latency as u64+o.loss as u64*p.loss as u64+(1000000000/(o.goodput/1000+1)).min(1000000)*p.throughput as u64+o.cost as u64*p.cost as u64}
pub fn select(r:Requirements,p:Policy,observations:&[Observation;6],now:u64)->Choice{
 if !valid_policy(p)||!valid_requirements(r){return Choice{status:4,..Default::default()};}
 observations.iter().copied().enumerate().filter(|(_,o)|eligible(*o,p,r,now)).min_by_key(|(_,o)|((r.preferred&!o.properties).count_ones(),score(*o,p),o.id)).map(|(index,o)|Choice{id:o.id,score:score(o,p),profile:o.profile,properties:o.properties,index:index as u32,status:0}).unwrap_or(Choice{status:6,..Default::default()})
}
struct Reader<'a>{bytes:&'a[u8],position:usize}
impl Reader<'_>{fn u32(&mut self)->Result<u32>{let n=self.bytes.get(self.position..self.position+4).ok_or(Error::Invalid("short certificate"))?;self.position+=4;Ok(u32::from_be_bytes(n.try_into().unwrap()))}fn u64(&mut self)->Result<u64>{let a=self.u32()? as u64;Ok(a<<32|self.u32()? as u64)}}
#[derive(Clone,Debug)]
pub struct SelectionReport{pub choice:Choice,pub rejected:usize,pub digest:String}
impl SelectionReport{pub fn json(&self)->String{format!("{{\"status\":\"passed\",\"model\":\"mandatory_property_filter_and_optimal_ranking\",\"selected\":{},\"profile\":{},\"score\":{},\"choice_status\":{},\"rejected\":{},\"certificate_sha256\":\"{}\",\"runtime_observations_are_assumptions\":true,\"native_pending\":true}}",self.choice.id,self.choice.profile,self.choice.score,self.choice.status,self.rejected,self.digest)}}
pub fn check_selection(bytes:&[u8],work:usize)->Result<SelectionReport>{
 if work<6{return Err(Error::UnknownWithinBudget)}need(bytes.len()==456 && bytes.starts_with(b"DOT1"),"invalid selection certificate size/magic")?;let mut input=Reader{bytes,position:4};let r=Requirements{required:input.u32()?,forbidden:input.u32()?,preferred:input.u32()?};let p=Policy{delivery:input.u32()?,latency:input.u32()?,loss:input.u32()?,throughput:input.u32()?,cost:input.u32()?,max_age:input.u32()?,min_samples:input.u32()?,min_hold:input.u32()?,hysteresis:input.u32()?};let now=input.u64()?;need(input.u32()?==6,"wrong candidate count")?;let claimed=Choice{id:input.u64()?,score:input.u64()?,profile:input.u32()?,properties:input.u32()?,index:input.u32()?,status:input.u32()?};let mut values=[Observation::default();6];for o in &mut values{*o=Observation{id:input.u64()?,at:input.u64()?,rtt:input.u64()?,goodput:input.u64()?,samples:input.u64()?,loss:input.u32()?,profile:input.u32()?,properties:input.u32()?,cost:input.u32()?,ready:match input.u32()?{0=>false,1=>true,_=>return Err(Error::Invalid("noncanonical boolean"))}};}
 let expected=select(r,p,&values,now);need(expected==claimed,"forged selection, score, properties or rejection status")?;let rejected=values.iter().filter(|o|!valid_policy(p)||!valid_requirements(r)||!eligible(**o,p,r,now)).count();let digest=dlm_network_proof::sha256(bytes);Ok(SelectionReport{choice:expected,rejected,digest})
}
#[derive(Clone,Copy,Debug,Hash,PartialEq,Eq)]
struct State{accepted:u8,wal:u8,received:u8,delivered:u8,acked:u8,ready:u8,active:u8}
#[derive(Clone,Debug)]
pub struct MigrationReport{pub messages:u32,pub window:u32,pub delivery:u32,pub states:usize,pub transitions:usize,pub work:usize}
impl MigrationReport{pub fn json(&self)->String{format!("{{\"status\":\"passed\",\"model\":\"bounded_migration_ack_and_dedup\",\"messages\":{},\"window\":{},\"delivery\":{},\"states\":{},\"transitions\":{},\"work\":{},\"model_invariants\":31,\"fair_delivery_is_an_assumption\":true,\"native_pending\":true}}",self.messages,self.window,self.delivery,self.states,self.transitions,self.work)}}
fn prefix(bits:u8,max:u32)->u8{let mut mask=0u8;for i in 0..max{let bit=1u8<<i;if bits&bit==0{break}mask|=bit;}mask}
pub fn check_migration(messages:u32,window:u32,delivery:u32,budget:usize)->Result<MigrationReport>{
 need((1..=4).contains(&messages)&&(1..=messages).contains(&window)&&(delivery==1||delivery==2),"finite model limit")?;let initial=State{accepted:0,wal:0,received:0,delivered:0,acked:0,ready:0,active:2};let mut seen=HashSet::new();seen.insert(initial);let mut queue=VecDeque::from([initial]);let mut work=0;let mut transitions=0;let mut finished=false;
 while let Some(s)=queue.pop_front(){let all=if s.accepted==0{0}else{((1u16<<s.accepted)-1) as u8};need(s.accepted<=messages as u8 && s.wal==(all&!s.acked) && s.acked&!s.received==0 && s.delivered&!s.received==0 && s.received&!all==0 && s.wal.count_ones()<=window,"WAL/ACK/delivery invariant")?;need(s.active==2||s.ready&(1<<s.active)!=0,"data path precedes authentication")?;finished|=s.accepted==messages as u8 && s.delivered==(1u8<<messages)-1;let mut next=Vec::new();
  if s.accepted<messages as u8 && s.wal.count_ones()<window{let mut n=s;n.wal|=1<<n.accepted;n.accepted+=1;next.push(n);}
  for path in 0..2{if s.ready&(1<<path)==0{let mut n=s;n.ready|=1<<path;next.push(n);}else{let mut n=s;n.active=path;next.push(n);let mut n=s;n.ready&=!(1<<path);if n.active==path{n.active=2;}need(n.wal==s.wal,"migration discards unacknowledged data")?;next.push(n);}}
  if s.active!=2{for id in 0..s.accepted{let bit=1u8<<id;if s.wal&bit!=0 && s.received&bit==0 && (id as u32)<(prefix(s.delivered,messages).count_ones()+window){let mut n=s;n.received|=bit;next.push(n);}}let mut n=s;n.acked=prefix(s.received,messages);n.wal=all&!n.acked;next.push(n);}
  for id in 0..s.accepted{let bit=1u8<<id;if s.received&bit!=0 && s.delivered&bit==0 && (delivery==2 || s.delivered==bit-1){let mut n=s;n.delivered|=bit;next.push(n);}}
  for n in next{if work==budget || seen.len()>=100000{return Err(Error::UnknownWithinBudget)}work+=1;transitions+=1;if seen.insert(n){queue.push_back(n);}}
 }
 need(finished,"completion unreachable under fair delivery")?;Ok(MigrationReport{messages,window,delivery,states:seen.len(),transitions,work})
}
pub fn check_migration_certificate(bytes:&[u8],budget:usize)->Result<MigrationReport>{need(bytes.len()==36 && bytes.starts_with(b"DML1"),"invalid migration certificate size/magic")?;let mut r=Reader{bytes,position:4};let messages=r.u32()?;let window=r.u32()?;let delivery=r.u32()?;let states=r.u64()?;let edges=r.u64()?;need(r.u32()?==31,"forged migration property mask")?;let report=check_migration(messages,window,delivery,budget)?;need(report.states as u64==states && report.transitions as u64==edges,"forged migration exploration counts")?;Ok(report)}
