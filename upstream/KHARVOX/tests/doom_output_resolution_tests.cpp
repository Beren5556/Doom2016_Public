#include "../src/overlay/DoomOutputResolution.h"
#include <stdexcept>
#include <iostream>
#include <limits>
using namespace kharvox::overlay;
static void need(bool ok){if(!ok)throw std::runtime_error("Output transaction invariant failed");}
int main(){
    OutputDescription d;d.recommended={OutputSize{3200,3400},OutputSize{3200,3400}};
    d.maximum={OutputSize{8192,8192},OutputSize{8192,8192}};d.selected=d.recommended;d.scale=1;d.writable=true;
    need(!d.plan(std::numeric_limits<float>::quiet_NaN()));need(!d.plan(.49f));need(!d.plan(3.1f));
    need(!d.plan(2.f)); // 6400 x 6800 exceeds the renderer pixel budget.
    auto p=d.plan(3250.f/3200);need(bool(p));need(p->eyes[0].width==3250);need(p->source.height%2==0);
    need(d.maximumWidth()>3200&&d.maximumWidth()<6400);
    // Asymmetric runtime maxima constrain the same shared output slider.
    d.maximum[1]={3300,3500};need(!d.plan(1.1f));need(d.maximumWidth()<3300);
    d.maximum[1]={8192,8192};
    OutputResize transaction;transaction.describe(d);
    need(transaction.request(2,3250.f/3200,100));need(!transaction.request(3,1.1f,101));
    need(!transaction.presented(9,{p->source,p->source}));need(!transaction.xrCommitted(2));
    need(transaction.prepared(2));need(!transaction.engineCreated({1,1},9));
    need(transaction.engineCreated(p->source,9));need(!transaction.presented(9,{p->source,p->source}));
    need(transaction.describe().selected[0].width==3200);
    need(transaction.xrCommitted(2));need(transaction.describe().selected[0].width==3250);
    need(!transaction.presented(8,{p->source,p->source}));need(transaction.snapshot().phase==OutputResize::Phase::Frame);
    need(!transaction.presented(9,{p->source,OutputSize{1,1}}));
    // Geometry confirmation also works when SR fell back to the engine TAA.
    // Provider evaluations are deliberately not part of this receipt.
    need(transaction.presented(9,{p->source,p->source}));need(transaction.snapshot().phase==OutputResize::Phase::Accepted);
    need(!transaction.request(2,1.f,103));need(transaction.request(3,1.1f,104));
    transaction.reject(2,"stale failure");need(transaction.snapshot().phase==OutputResize::Phase::Queued);
    transaction.reject(3,"allocation refused");need(transaction.snapshot().phase==OutputResize::Phase::Rejected);
    need(transaction.describe().selected[0].width==3250); // Rejection never invents an applied size.
    transaction.disable();need(!transaction.request(4,1.f,105));
    std::cout<<"PASS output limits, paired dimensions, engine/XR/frame receipts, stale requests and rejection\n";
}
