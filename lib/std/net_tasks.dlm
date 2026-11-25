module std.net_tasks
import std.net_async

native {
    record NetworkTaskControl repr(C) { operation: U64; completion: Sender<ReactorCompletion>; state: U32; }
    record NetworkTask repr(C) { future: Future<ReactorCompletion>; control: NetworkTaskControl; }

    fn scope_task(operation: NetworkOperation) -> NetworkTask {
        let sender: Sender<ReactorCompletion>=channel<ReactorCompletion>(1);
        let receiver: Receiver<ReactorCompletion>=channel_receiver(&sender);
        let future: Future<ReactorCompletion>=receive_async(move receiver);
        let mut state: U32=0;
        if operation.id==0 || operation.error.kind!=0 {
            let mut result: ReactorCompletion=zero<ReactorCompletion>();
            result.operation=operation.id;result.transfer.error=operation.error;
            if result.transfer.error.kind==0 { result.transfer.error.kind=4; }
            let delivered: Bool=try_send(&sender,result);assert(delivered);state=1;
        }
        return NetworkTask(move future,NetworkTaskControl(operation.id,move sender,state));
    }

    fn scope_task_poll(scope: &mut NetworkScope, control: &mut NetworkTaskControl) -> U32 {
        if control.state==1 || control.state==2 { return control.state; }
        if control.state==0 && !sender_connected(&control.completion) {
            let cancelled: NetworkTransfer=scope_cancel(scope,control.operation);control.state=3;
        }
        let result: ReactorCompletion=scope_result(scope,control.operation);
        if result.transfer.error.kind==1 { return control.state; }
        if control.state==3 {
            let released: NetworkTransfer=scope_ack(scope,control.operation);control.state=2;return 2;
        }
        if try_send(&control.completion,result) { control.state=1;return 1; }
        let released: NetworkTransfer=scope_ack(scope,control.operation);control.state=2;return 2;
    }
}
