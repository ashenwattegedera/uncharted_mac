#include "guest_fault.h"
static int allowed(BbGuestMemory *vm,const BbGuestFault *f,int effective) {
    for(size_t i=0;i<f->size;) {
        BbVmView view;
        if(bb_vm_query(vm,f->address+i,&view)!=BB_VM_OK ||
           !((effective ? view.effective_permissions : view.guest_permissions)&f->access)) return 0;
        size_t part=view.size<f->size-i ? view.size : f->size-i;
        i+=part;
    }
    return 1;
}
BbFaultResult bb_guest_fault_route(BbFaultRouter *r,const BbGuestFault *f) {
    if(!r || !f || !r->memory || r->active || bb_vm_status(r->memory)!=BB_VM_OK) return BB_FAULT_ERROR;
    if(!f->pc || !f->size || f->size>BB_VM_GUEST_PAGE ||
       (f->access!=BB_VM_READ && f->access!=BB_VM_WRITE && f->access!=BB_VM_EXEC) ||
       !bb_vm_contains(r->memory,f->address,f->size)) return BB_FAULT_UNHANDLED;
    r->active=1;
    BbFaultResult result=BB_FAULT_UNHANDLED;
    BbVmView view;
    if(bb_vm_query(r->memory,f->address,&view)!=BB_VM_OK) {
        if(r->demand_map && r->demand_map(r->user,f) && allowed(r->memory,f,1)) result=BB_FAULT_RECOVERED;
        goto done;
    }
    /* GPU watches cannot grant a permission forbidden by the guest itself. */
    if(f->access!=BB_VM_WRITE || !allowed(r->memory,f,0) || !r->gpu_write) goto done;
    size_t granule=bb_vm_host_page(r->memory);
    if(granule<BB_VM_GUEST_PAGE) granule=BB_VM_GUEST_PAGE;
    uint64_t first=f->address-(f->address%granule),end=f->address+f->size;
    unsigned found=0;
    for(uint64_t at=first;at<end;at+=granule) {
        for(size_t offset=0;offset<granule;offset+=BB_VM_TRACKER_PAGE) {
            if(bb_vm_query(r->memory,at+offset,&view)!=BB_VM_OK) goto done;
            if(view.write_watchers) {
                if(!r->gpu_write(r->user,at+offset,view.backing_offset,view.write_watchers)) goto done;
                ++found;
            }
        }
    }
    if(!found) goto done;
    for(uint64_t at=first;at<end;at+=granule) {
        if(bb_vm_clear_write_watches(r->memory,at,granule)!=BB_VM_OK) { result=BB_FAULT_ERROR; goto done; }
    }
    if(allowed(r->memory,f,1)) result=BB_FAULT_RECOVERED;
done:
    r->active=0; return result;
}
