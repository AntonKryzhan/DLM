use std::path::PathBuf;
use dlm_network_proof::{check_bundle,NetworkBudget};
fn main(){
    let args=std::env::args().skip(1).collect::<Vec<_>>();let result=(||{
        let path=PathBuf::from(args.first().ok_or("usage: dlm-network-check FILE.nproof --lib DIR [--binary FILE] [--work N]")?);let mut library=PathBuf::from("lib");let mut binary=None;let mut budget=NetworkBudget::default();let mut index=1;
        while index<args.len(){let option=&args[index];let value=args.get(index+1).ok_or("missing option value")?;match option.as_str(){"--lib"=>library=PathBuf::from(value),"--binary"=>binary=Some(PathBuf::from(value)),"--work"=>budget.max_work=value.parse().map_err(|_|"invalid work budget")?,_=>return Err("unknown option".to_string())}index+=2;}
        check_bundle(&path,&library,binary.as_deref(),budget).map_err(|e|e.to_string())
    })();match result{Ok(report)=>println!("{}",report.json()),Err(error)=>{eprintln!("network proof: {error}");std::process::exit(if error.contains("UnknownWithinBudget"){3}else{1});}}
}
