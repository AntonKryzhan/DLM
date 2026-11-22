//! Fixed-rule network state kernel. No dependency on the compiler, VM or search.
mod digest;
mod binding;
pub use digest::sha256;
pub use binding::{check_bundle, runtime_bindings, source_matches, NetworkCertificate, MAX_CERTIFICATE_BYTES};
use std::fmt;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ErrorKind { Invalid, UnknownWithinBudget }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error { pub kind: ErrorKind, pub message: String }
impl fmt::Display for Error { fn fmt(&self,f:&mut fmt::Formatter<'_>)->fmt::Result { write!(f,"{:?}: {}",self.kind,self.message) } }
impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T,Error>;
pub(crate) fn invalid(message: impl Into<String>) -> Error { Error{kind:ErrorKind::Invalid,message:message.into()} }
fn require(value:bool,message:&str)->Result<()> { if value {Ok(())}else{Err(invalid(message))} }
#[derive(Clone, Copy, Debug)]
pub struct NetworkBudget { pub max_work: usize, pub max_certificate_bytes: usize }
impl Default for NetworkBudget { fn default()->Self { Self{max_work:200_000,max_certificate_bytes:512*1024} } }
struct Work { remaining: usize }
impl Work { fn tick(&mut self)->Result<()> { if self.remaining==0 {Err(Error{kind:ErrorKind::UnknownWithinBudget,message:"network kernel work budget exhausted".into()})}else{self.remaining-=1;Ok(())} } }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkFieldIR(pub [u32;8]);
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkMessageIR { pub words: [u32;6], pub fields: Vec<NetworkFieldIR> }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkSpecIR { pub words: [u32;33], pub messages: Vec<NetworkMessageIR> }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkStateIR(pub [u32;5]);
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkTransitionIR(pub [u32;8]);
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkExecutableIR { pub initial: u32, pub states: Vec<NetworkStateIR>, pub transitions: Vec<NetworkTransitionIR> }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkProofIR { pub reachable: u32, pub ranks: Vec<u32>, pub properties: u32 }
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NetworkAudit { pub checked_states: usize, pub checked_transitions: usize, pub model_verified: u32, pub runtime_pending: u32, pub work: usize, pub source_sha256: String, pub binary_sha256: Option<String>, pub assumptions: Vec<String> }
impl NetworkAudit {
    pub fn json(&self)->String {
        let assumptions=self.assumptions.iter().map(|s|format!("\"{s}\"")).collect::<Vec<_>>().join(",");let binary=self.binary_sha256.as_ref().map(|s|format!("\"{s}\"")).unwrap_or_else(||"null".into());
        format!("{{\"status\":\"checked\",\"scope\":\"finite protocol state model\",\"states\":{},\"transitions\":{},\"model_verified\":{},\"runtime_pending\":{},\"work\":{},\"source_sha256\":\"{}\",\"binary_sha256\":{},\"assumptions\":[{}],\"native_correctness_proved\":false}}",self.checked_states,self.checked_transitions,self.model_verified,self.runtime_pending,self.work,self.source_sha256,binary,assumptions)
    }
}
struct Reader<'a>{data:&'a[u8],position:usize}
impl<'a> Reader<'a>{
    fn new(data:&'a[u8],magic:&[u8],maximum:usize)->Result<Self>{require(data.len()>=magic.len() && data.len()<=maximum && data.starts_with(magic),"invalid network IR framing")?;Ok(Self{data,position:magic.len()})}
    fn words<const N:usize>(&mut self)->Result<[u32;N]>{require(N<=(self.data.len()-self.position)/4,"truncated network IR")?;let mut out=[0;N];for word in &mut out{*word=u32::from_be_bytes(self.data[self.position..self.position+4].try_into().unwrap());self.position+=4;}Ok(out)}
    fn end(&self)->Result<()>{require(self.position==self.data.len(),"trailing network IR bytes")}
}
impl NetworkFieldIR {
    pub fn measure(&self,wire:u32)->(u64,u64){let f=self.0;let(mut low,mut high)=match f[1]{1|5=>(1,1),2=>(2,2),3=>(4,4),4=>(8,8),6|7=>(0,f[4] as u64),8|9=>(1,10),_=>return(0,0)};let mut prefix=0;if wire==2{low+=8;high+=8;}else{if f[1]==6||f[1]==7{low+=4;high+=4;}if f[2]!=1||f[3]!=1{prefix=4;}}(prefix+low*f[2] as u64,prefix+high*f[3] as u64)}
    pub fn active(&self,minor:u32)->bool{self.0[5]<=minor && minor<=self.0[6]}
}
impl NetworkSpecIR {
    pub fn from_bytes(data:&[u8])->Result<Self>{let mut input=Reader::new(data,b"DNS1",16904)?;let words=input.words::<33>()?;require((1..=16).contains(&words[32]),"invalid message count")?;let mut messages=Vec::new();for _ in 0..words[32]{let h=input.words::<6>()?;require(h[5]<=32,"invalid field count")?;let mut fields=Vec::new();for _ in 0..h[5]{fields.push(NetworkFieldIR(input.words::<8>()?));}messages.push(NetworkMessageIR{words:h,fields});}input.end()?;let result=Self{words,messages};result.validate()?;Ok(result)}
    pub fn to_bytes(&self)->Vec<u8>{let mut out=b"DNS1".to_vec();put(&mut out,&self.words);for message in &self.messages{put(&mut out,&message.words);for field in &message.fields{put(&mut out,&field.0);}}out}
    pub fn validate(&self)->Result<()> {
        let w=self.words;require(w[0]!=0 && (1..=65535).contains(&w[1]) && w[3]<=w[2] && w[2]<=65535,"invalid protocol/version")?;
        require((w[4]==1 && (w[5]==1||w[5]==2) && w[6]==0 && w[7]==0)||(w[4]==2 && w[5]==2 && (w[6]==1||w[6]==2) && w[7]<=1),"invalid wire profile")?;
        require((w[8]|w[9]|w[10])&!511==0 && w[8]&w[9]==0 && w[10]&w[9]==0 && w[8]&66!=66,"contradictory transport requirements")?;
        require(w[11]<=16777216 && (1..=4096).contains(&w[12]) && (1..=256).contains(&w[13]) && w[14]==0 && (1024..=67108864).contains(&w[15]) && 1024+w[13] as u64*256+2*(w[11] as u64+128)<=w[15] as u64,"invalid transport bounds")?;
        require(w[16]<=4096 && w[17]<=w[11] && (w[18]!=0||w[19]!=0) && (1..=529).contains(&w[20]),"invalid resource bounds")?;
        require((w[7]==0 && w[16]==0 && w[17]==0)||(w[7]==1 && w[16]>=1 && w[17]>=8),"invalid unknown-field budget")?;
        require(w[21]>0 && w[21]<=w[22] && w[22]<=w[23] && w[23]<=i32::MAX as u32 && w[24]>0 && w[24]<=w[12] && w[25]<=1,"invalid session contract")?;
        require(w[26]<=2 && w[27]<=1 && w[28]<=1 && w[29]<=2 && (w[25]==0||w[26]!=0) && w[30]&!2047==0 && (w[31]==1||w[31]==2),"invalid security/proof contract")?;
        require(self.messages.len()==w[32] as usize && (1..=16).contains(&w[32]),"wrong message count")?;let mut previous=0;let mut nodes=1;let mut intervals=Vec::new();let mut peak=0u64;let mut sensitive=false;
        for message in &self.messages {
            let h=message.words;require(previous<h[0] && (1..=3).contains(&h[1]) && (1..=3).contains(&h[2]) && h[3]<=h[4] && h[3]<=w[2] && h[4]>=w[3] && message.fields.len()==h[5] as usize && h[5]<=32,"invalid message declaration")?;previous=h[0];nodes+=1;intervals.push((h[3].max(w[3]),h[4].min(w[2])));let mut prior=0;let mut points=vec![h[3].max(w[3])];
            for field in &message.fields {let f=field.0;require(prior<f[0] && (1..=9).contains(&f[1]) && f[2]<=f[3] && (1..=4096).contains(&f[3]) && f[4]<=16777216 && f[5]<=f[6] && f[7]<=1 && ((f[1]==6||f[1]==7)||f[4]==0),"invalid field declaration")?;require(h[3]<=f[5] && f[5]<=w[2] && f[6]<=h[4] && f[6]>=w[3],"invalid field version interval")?;prior=f[0];nodes+=1;sensitive|=f[7]!=0;points.push(f[5]);if f[6]!=u32::MAX{points.push(f[6]+1);}}
            for point in points {if h[3].max(w[3])<=point && point<=h[4].min(w[2]){let size=(if w[4]==2{12}else{4})+message.fields.iter().filter(|f|f.active(point)).map(|f|f.measure(w[4]).1).sum::<u64>();peak=peak.max(size);}}
        }
        intervals.sort();let mut cursor=w[3];for(start,end)in intervals{require(start<=cursor,"uncovered minor version")?;cursor=cursor.max(end+1);}require(cursor>w[2] && nodes<=w[20] && peak+w[17] as u64<=w[11] as u64,"schema budget/version coverage failed")?;
        let secure=sensitive||w[26..30].iter().any(|v|*v!=0);let effective=w[8]|16|if secure{4}else{0};require(effective&w[9]==0 && (effective&8==0||w[13]>=2),"effective transport requirement conflict")
    }
    pub fn pending(&self)->u32{let w=self.words;431|w[30]|if w[7]!=0{16}else{0}|if w[25]!=0{64}else{0}|if self.secure(){1024}else{0}}
    pub fn secure(&self)->bool{self.words[8]&4!=0||self.words[26..30].iter().any(|v|*v!=0)||self.messages.iter().any(|m|m.fields.iter().any(|f|f.0[7]!=0))}
    pub fn message(&self,id:u32,minor:u32)->Option<&NetworkMessageIR>{self.messages.iter().find(|m|m.words[0]==id && m.words[3]<=minor && minor<=m.words[4])}
}
fn put(out:&mut Vec<u8>,words:&[u32]){for value in words{out.extend_from_slice(&value.to_be_bytes());}}
impl NetworkExecutableIR {
    pub fn from_bytes(data:&[u8])->Result<Self>{let mut input=Reader::new(data,b"DNM1",8848)?;let h=input.words::<3>()?;require((1..=32).contains(&h[1]) && (1..=256).contains(&h[2]),"machine table limit")?;let mut states=Vec::new();let mut transitions=Vec::new();for _ in 0..h[1]{states.push(NetworkStateIR(input.words::<5>()?));}for _ in 0..h[2]{transitions.push(NetworkTransitionIR(input.words::<8>()?));}input.end()?;Ok(Self{initial:h[0],states,transitions})}
    pub fn to_bytes(&self)->Vec<u8>{let mut out=b"DNM1".to_vec();put(&mut out,&[self.initial,self.states.len() as u32,self.transitions.len() as u32]);for s in &self.states{put(&mut out,&s.0);}for t in &self.transitions{put(&mut out,&t.0);}out}
    pub fn index(&self,id:u32)->Option<usize>{self.states.iter().position(|s|s.0[0]==id)}
}
pub fn check_network_proof(spec:&NetworkSpecIR,machine:&NetworkExecutableIR,minor:u32,proof:&NetworkProofIR,budget:NetworkBudget)->Result<NetworkAudit>{
    let mut work=Work{remaining:budget.max_work};work.tick()?;spec.validate()?;require(spec.words[3]<=minor && minor<=spec.words[2],"minor outside accepted window")?;let count=machine.states.len();require((1..=32).contains(&count) && (1..=256).contains(&machine.transitions.len()),"machine table limit")?;let initial=machine.index(machine.initial).ok_or_else(||invalid("missing initial state"))?;
    require(machine.states[initial].0==[machine.initial,0,0,0,0],"invalid initial state")?;let all=if count==32{u32::MAX}else{(1u32<<count)-1};require(proof.reachable==all && proof.ranks.len()==count && proof.ranks[initial]==0,"invalid reachability witness")?;
    let mut terminal=false;
    for (index,state) in machine.states.iter().enumerate(){work.tick()?;let s=state.0;require(s[0]!=0 && s[1]<=6 && s[2]<=1 && s[3]<=1 && s[4]<=1 && machine.index(s[0])==Some(index),"invalid state")?;
        if s[4]==0{require((s[1]!=0||(s[0]==machine.initial && s[2]==0 && s[3]==0)) && (s[1]==0||s[2]==1) && (s[1]!=1||s[3]==0) && (s[1]<3||s[3]!=0),"state does not represent the owned connection lifecycle")?;let required=match s[1]{0=>1,1=>2,2=>3,3=>6,4=>11,_=>0};for role in [1,2]{for event in [required,7]{let mut found=0;for edge in &machine.transitions{work.tick()?;let t=edge.0;if t[0]==s[0] && t[1]==event && t[2]==0 && t[3]&role!=0{found+=1;}}require(found==1,"missing or ambiguous lifecycle transition")?;}}}
        if s[4]!=0{terminal=true;require(s[2]==0 && s[3]==0 && (s[1]==5||s[1]==6),"terminal retains a resource/authentication")?;}else{require(s[1]<5,"nonterminal final phase")?;for event in 8..=10{for role in [1,2]{let mut found=0;for edge in &machine.transitions{work.tick()?;let t=edge.0;if t[0]==s[0] && t[1]==event && t[2]==0 && t[3]&role!=0{if let Some(to)=machine.index(t[4]){if machine.states[to].0[4]!=0{found+=1;}}}}require(found==1,"missing or ambiguous timeout/error/cancel cleanup")?;}}}
        if index!=initial{require(proof.ranks[index]>0 && proof.ranks[index]<(count as u32),"invalid reachability rank")?;let mut parent=false;for edge in &machine.transitions{work.tick()?;if edge.0[4]==s[0]{if let Some(from)=machine.index(edge.0[0]){parent|=proof.ranks[from]<proof.ranks[index];}}}require(parent,"reachability witness has no lower-rank predecessor")?;}
    }
    require(terminal,"no terminal state")?;
    for (index,edge) in machine.transitions.iter().enumerate(){work.tick()?;let t=edge.0;let from=machine.index(t[0]).ok_or_else(||invalid("undefined source state"))?;let to=machine.index(t[4]).ok_or_else(||invalid("undefined target state"))?;let a=machine.states[from].0;let b=machine.states[to].0;
        require((1..=11).contains(&t[1]) && (1..=3).contains(&t[3]) && t[5]<=65535 && t[6]<=65535 && t[7]<=1,"invalid transition")?;require(a[4]==0 && t[5]&a[2]==0 && t[5]&t[6]==0 && t[6]&!a[2]==0 && (a[2]|t[5])&!t[6]==b[2],"resource transfer rule failed")?;
        require((b[4]!=0||b[1]>=a[1]) && (t[1]!=1||(a[1]==0&&b[1]==1)) && (t[1]!=2||(a[1]==1&&b[1]==2)) && (t[1]!=3||(a[1]==2&&(b[1]==2||b[1]==3)&&b[3]!=0)) && (t[1]!=6||((a[1]==3||a[1]==4)&&b[1]==4)) && (t[1]!=7||(b[4]!=0&&b[1]==5)) && (!(8..=10).contains(&t[1])||(b[4]!=0&&b[1]==6)) && (t[1]!=11||(a[1]==4&&b[4]!=0&&b[1]==5)) && (t[1]!=4||b[4]==0),"lifecycle event changes an incompatible phase")?;
        require(t[7]==0||a[3]!=0,"authentication guard without authentication")?;require(b[3]==0||a[3]!=0||t[1]==2||t[1]==3,"authentication elevated without establishment")?;require(b[4]!=0||a[3]==0||b[3]!=0,"authentication silently removed")?;
        if t[1]==4||t[1]==5{let message=spec.message(t[2],minor).ok_or_else(||invalid("undefined/inactive message"))?;let h=message.words;let roles=if t[1]==5{((h[2]&1)<<1)|((h[2]&2)>>1)}else{h[2]};require(t[3]&!roles==0 && (h[1]!=1||a[1]==2) && (h[1]!=2||a[1]==3) && (h[1]!=3||a[1]==3||a[1]==4),"message role/phase rule failed")?;let protected=(spec.words[25]!=0 && h[1]==2)||message.fields.iter().any(|f|f.active(minor)&&f.0[7]!=0);require(!protected||(a[3]!=0&&t[7]!=0),"protected operation precedes authentication")?;}else{require(t[2]==0,"control event carries a message")?;}
        for previous in &machine.transitions[..index]{work.tick()?;let p=previous.0;require(!(p[0]==t[0]&&p[1]==t[1]&&p[2]==t[2]&&p[3]&t[3]!=0),"nondeterministic transition")?;}
    }
    let verified=416|if spec.words[25]!=0{64}else{0};require(proof.properties==verified,"forged property mask")?;
    Ok(NetworkAudit{checked_states:count,checked_transitions:machine.transitions.len(),model_verified:verified,runtime_pending:spec.pending(),work:budget.max_work-work.remaining,source_sha256:String::new(),binary_sha256:None,assumptions:vec!["verified transport enforces declared security".into(),"external trust: dlm.net.v1, libcurl TLS, ngtcp2 QUIC, GnuTLS and OS entropy".into(),"clock is monotonic and polling continues".into(),"timeouts and failures execute the modeled cleanup".into(),"local in-flight means queued transport messages, not peer acknowledgements".into(),"pinned runtime and compiler faithfully execute the state IR".into(),"foreign handles and arbitrary representation forgery are excluded".into()]})
}
