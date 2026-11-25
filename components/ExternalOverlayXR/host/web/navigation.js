'use strict';
// Common navigation only: never rebuild controls, reset a draft or restart XR.
(()=>{
 const buttons=[...document.querySelectorAll('[data-overlay-page]')];
 const views=[...document.querySelectorAll('[data-overlay-view]')];
 let active='parameters';const scroll={};
 for(const button of buttons)button.addEventListener('click',()=>{
  const page=button.dataset.overlayPage;if(page===active)return;
  scroll[active]=window.scrollY;active=page;
  for(const view of views)view.hidden=view.dataset.overlayView!==page;
  for(const item of buttons){const selected=item===button;item.classList.toggle('selected',selected);item.setAttribute('aria-pressed',String(selected));}
  document.getElementById('pv-actions').hidden=page!=='parameters';
  window.scrollTo(0,scroll[page]||0);
  document.dispatchEvent(new CustomEvent('overlaypagechange',{detail:page}));
 });
})();
