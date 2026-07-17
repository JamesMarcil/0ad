var g_LoadingPage;

async function init(data)
{
	g_LoadingPage = {
		"initData": data,
		"progressBar": new ProgressBar(),
		"quoteDisplay": new QuoteDisplay(),
		"tipDisplay": new TipDisplay({ "tipScrolling": false, "isOnLoadingScreen": true }),
		"titleDisplay": new TitleDisplay(data)
	};

	Engine.SetCursor("cursor-wait");

	// reallyStartGame and cancelOnLoadGameError are reserved names that are called by the engine.
	// reallyStartGame is called when it is ready to start the game (i.e. loading progress has reached
	// 100%).
	// cancnelOnLoadGameError is called when there is an error.
	return new Promise(closePageCallback =>
	{
		globalThis.reallyStartGame = () =>
		{
			Engine.ResetCursor();
			closePageCallback({ [Engine.openRequest]: {
				"page": "page_session.xml",
				"argument": data
			} });
		};
		globalThis.cancelOnLoadGameError = async(errorMessage) =>
		{
			Engine.ResetCursor();
			Engine.EndGame();

			if (Engine.HasXmppClient())
				Engine.StopXmppClient();

			await Engine.OpenChildPage("page_msgbox.xml", {
				"width": 500,
				"height": 200,
				"message": errorMessage,
				"title": translate("Loading Aborted"),
				"mode": 2
			});

			closePageCallback({ [Engine.openRequest]: { "page": "page_pregame.xml" } });
		};
	});
}
