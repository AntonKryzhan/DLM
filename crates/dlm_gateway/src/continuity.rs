use crate::{ensure, Error, Result};
use dlm_network_proof::{
    check_network_proof, sha256, NetworkBudget, NetworkExecutableIR, NetworkFieldIR,
    NetworkMessageIR, NetworkProofIR, NetworkSpecIR, NetworkStateIR, NetworkTransitionIR,
};
use std::collections::{BTreeSet, VecDeque};
pub fn model() -> (NetworkSpecIR, NetworkExecutableIR, NetworkProofIR) {
    let mut spec = crate::protocol::specification();
    for id in 21..=25 {
        spec.messages.push(NetworkMessageIR {
            words: [id, 2, 3, 0, 3, 1],
            fields: vec![NetworkFieldIR([1, 6, 1, 1, 512, 0, 3, 1])],
        });
    }
    spec.words[32] = spec.messages.len() as u32;
    let states = vec![
        NetworkStateIR([1, 0, 0, 0, 0]),
        NetworkStateIR([2, 1, 1, 0, 0]),
        NetworkStateIR([3, 2, 1, 0, 0]),
        NetworkStateIR([4, 3, 1, 1, 0]),
        NetworkStateIR([5, 3, 1, 1, 0]),
        NetworkStateIR([6, 3, 1, 1, 0]),
        NetworkStateIR([7, 3, 1, 1, 0]),
        NetworkStateIR([8, 3, 1, 1, 0]),
        NetworkStateIR([9, 4, 1, 1, 0]),
        NetworkStateIR([10, 5, 0, 0, 1]),
        NetworkStateIR([11, 6, 0, 0, 1]),
    ];
    let mut transitions = Vec::new();
    let mut edge = |from, event, message, to, acquire, release, auth| {
        transitions.push(NetworkTransitionIR([
            from, event, message, 3, to, acquire, release, auth,
        ]))
    };
    edge(1, 1, 0, 2, 1, 0, 0);
    edge(2, 2, 0, 3, 0, 0, 0);
    edge(3, 3, 0, 4, 0, 0, 0);
    for state in 1..=9 {
        edge(state, 7, 0, 10, 0, if state == 1 { 0 } else { 1 }, 0);
        for event in 8..=10 {
            edge(state, event, 0, 11, 0, if state == 1 { 0 } else { 1 }, 0);
        }
    }
    for state in 4..=8 {
        edge(state, 6, 0, 9, 0, 0, 1);
        edge(state, 4, 7, state, 0, 0, 1);
        edge(state, 5, 8, state, 0, 0, 1);
        edge(state, 5, 25, 4, 0, 0, 1);
    }
    for (message, target) in [(21, 5), (22, 6), (23, 7), (24, 8)] {
        edge(4, 5, message, target, 0, 0, 1);
    }
    edge(9, 11, 0, 10, 0, 1, 1);
    for edge in &mut transitions {
        if edge.0[2] == 7 || edge.0[2] == 8 {
            edge.0[3] = 1;
        }
    }
    let proof = NetworkProofIR {
        properties: 480,
        reachable: (1 << 11) - 1,
        ranks: vec![0, 1, 2, 3, 4, 4, 4, 4, 4, 1, 1],
    };
    (
        spec,
        NetworkExecutableIR {
            initial: 1,
            states,
            transitions,
        },
        proof,
    )
}
fn delivery_model() -> Result<(usize, usize)> {
    let mut seen = BTreeSet::new();
    let mut queue = VecDeque::from([0u32]);
    let mut edges = 0;
    const ALL: u32 = 15;
    while let Some(state) = queue.pop_front() {
        if !seen.insert(state) {
            continue;
        }
        ensure(
            seen.len() <= 65536,
            7,
            "delivery invariant model state budget exhausted",
        )?;
        let buffered = state & ALL;
        let committed = (state >> 4) & ALL;
        let acknowledged = (state >> 8) & ALL;
        let completed = state & (1 << 12) != 0;
        let confirmed = state & (1 << 13) != 0;
        ensure(
            acknowledged & !committed == 0
                && (!completed || committed == ALL)
                && (!confirmed || completed),
            12,
            "delivery model produced a false persistence acknowledgement",
        )?;
        let mut next = Vec::new();
        for bit in 0..4 {
            let flag = 1 << bit;
            if committed & flag == 0 {
                next.push(state | flag);
            }
            if buffered & flag != 0 {
                next.push((state & !flag) | (flag << 4));
            }
            if committed & flag != 0 {
                next.push(state | (flag << 8));
            }
        }
        next.push(state & !ALL);
        if committed == ALL {
            next.push(state | (1 << 12));
        }
        if completed {
            next.push(state | (1 << 13));
        }
        for target in next {
            edges += 1;
            if !seen.contains(&target) {
                queue.push_back(target);
            }
        }
    }
    Ok((seen.len(), edges))
}
pub fn audit() -> Result<String> {
    let (s, m, p) = model();
    let mut checked = check_network_proof(&s, &m, 1, &p, NetworkBudget::default())
        .map_err(|_| Error::new(12, "continuity ownership/authentication model rejected"))?;
    checked.source_sha256 = sha256(
        concat!(
            include_str!("path.rs"),
            include_str!("delivery.rs"),
            include_str!("service.rs")
        )
        .as_bytes(),
    );
    let (states, transitions) = delivery_model()?;
    Ok(format!("{{\"format\":\"DLN-CONTINUITY-MODEL-v1\",\"status\":\"PASS\",\"ownership_kernel\":{},\"delivery_scope\":\"four blocks; erasure, duplicate, persistence, acknowledgement, crash and restart\",\"delivery_states\":{states},\"delivery_transitions\":{transitions},\"false_committed_acks\":0,\"runtime_equivalence_proved\":false}}",checked.json()))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn independent_kernel_checks_degraded_recovering_buffered_and_offline_states() {
        let (s, m, p) = model();
        let checked = check_network_proof(&s, &m, 1, &p, NetworkBudget::default()).unwrap();
        assert_eq!(checked.checked_states, 11);
        assert_ne!(checked.runtime_pending, 0);
        assert!(audit().unwrap().contains("\"false_committed_acks\":0"));
    }
    #[test]
    fn altered_auth_guard_and_retained_terminal_resource_are_rejected() {
        let (s, mut m, p) = model();
        m.transitions.iter_mut().find(|t| t.0[2] == 21).unwrap().0[7] = 0;
        assert!(check_network_proof(&s, &m, 1, &p, NetworkBudget::default()).is_err());
        let (s, mut m, p) = model();
        m.states[10].0[2] = 1;
        assert!(check_network_proof(&s, &m, 1, &p, NetworkBudget::default()).is_err());
    }
}
