module examples.native.network_scope_control
import std.net_async

native {
    fn main() -> Unit {
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();assert(network_cancel_valid(&cancel));
        let mut group: NetworkScope=scope_open(1,4,32,1);
        let first: NetworkOperation=scope_event(&mut group,3000);assert(network_ok(&first.error));
        let second: NetworkOperation=scope_event(&mut group,3000);assert(network_ok(&second.error));
        let ready: NetworkOperation=scope_timer(&mut group,0);assert(network_ok(&ready.error));
        let later: NetworkOperation=scope_timer(&mut group,20);assert(network_ok(&later.error));
        let a: EventWaker=scope_event_waker(&group,first.id);let b: EventWaker=scope_event_waker(&group,second.id);
        let ea: NetworkError=event_waker_error(&a);let eb: NetworkError=event_waker_error(&b);assert(network_ok(&ea) && network_ok(&eb));
        let signal: NetworkTransfer=event_wake(&a);assert(network_ok(&signal.error) && signal.count==1);
        let duplicate: NetworkTransfer=event_wake(&a);assert(network_ok(&duplicate.error) && duplicate.count==0);
        let pressure: NetworkTransfer=event_wake(&b);assert(pressure.error.kind==7);
        let timeout: NetworkTransfer=scope_join(&mut group,1,&cancel);assert(timeout.error.kind==3);
        let result: ReactorCompletion=scope_result(&group,first.id);assert(network_ok(&result.transfer.error));
        let next: NetworkTransfer=event_wake(&b);assert(network_ok(&next.error));
        let joined: NetworkTransfer=scope_join(&mut group,3000,&cancel);assert(network_ok(&joined.error) && joined.count==4);
        let a0: NetworkTransfer=scope_ack(&mut group,first.id);let a1: NetworkTransfer=scope_ack(&mut group,second.id);
        let a2: NetworkTransfer=scope_ack(&mut group,ready.id);let a3: NetworkTransfer=scope_ack(&mut group,later.id);
        assert(network_ok(&a0.error) && network_ok(&a1.error) && network_ok(&a2.error) && network_ok(&a3.error));
        let pending: NetworkOperation=scope_event(&mut group,3000);assert(network_ok(&pending.error));
        let stop: OperationCanceller=scope_operation_canceller(&group,pending.id);
        let requested: NetworkTransfer=operation_cancel(&stop);assert(network_ok(&requested.error));
        let stopped: NetworkTransfer=scope_join(&mut group,3000,&cancel);assert(network_ok(&stopped.error));
        let cancelled: ReactorCompletion=scope_result(&group,pending.id);assert(network_was_cancelled(&cancelled.transfer.error));
        let all: ScopeCanceller=scope_canceller(&group);
        scope_close(move group);
        let closed: NetworkTransfer=event_wake(&a);let stopped_again: NetworkTransfer=operation_cancel(&stop);
        let closed_group: NetworkTransfer=scope_request_cancel(&all);
        assert(network_was_cancelled(&closed.error) && network_was_cancelled(&stopped_again.error) && network_was_cancelled(&closed_group.error));
        event_waker_close(move a);event_waker_close(move b);operation_canceller_close(move stop);scope_canceller_close(move all);
        resource_close(move cancel);
        unsafe { let shutdown: NetIo=network_provider_shutdown();assert(shutdown.status==0); }
        print("scoped wake cancellation and backpressure OK");
    }
}
