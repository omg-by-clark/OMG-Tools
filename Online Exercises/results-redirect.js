(() => {
  'use strict';

  const { buildResultsUrl } = window.OnlineExercises || {};
  if (typeof buildResultsUrl !== 'function') return;

  const params = new URLSearchParams(location.search);
  const packFile = params.get('pack');
  const requestedView = params.get('view');

  if (packFile && requestedView === 'results') {
    location.replace(buildResultsUrl(packFile));
    return;
  }

  const resultView = document.querySelector('#resultView');
  if (!resultView || !packFile) return;

  const redirect = () => {
    if (resultView.classList.contains('hidden')) return;
    if (!resultView.querySelector('.result-grid')) return;
    location.replace(buildResultsUrl(packFile));
  };

  const observer = new MutationObserver(redirect);
  observer.observe(resultView, { childList: true, subtree: true, attributes: true, attributeFilter: ['class'] });
})();
