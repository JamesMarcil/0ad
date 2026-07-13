globalThis.PlayerID = -1;

export class BaseAI
{
	constructor(settings)
	{
		if (!settings)
			return;

		this.player = settings.player;
	}

	/** Return a simple object (using no classes etc) that will be serialized into saved games */
	Serialize()
	{
		return {};
	}

	/**
	 * Called after the constructor when loading a saved game, with 'data' being
	 * whatever Serialize() returned
	 */
	Deserialize(data, sharedScript)
	{
	}

	Init(playerID, sharedAI)
	{
		PlayerID = playerID;

		this.territoryMap = sharedAI.territoryMap;
		this.accessibility = sharedAI.accessibility;

		this.gameState = sharedAI.gameState[this.player];
		this.gameState.ai = this;

		this.timeElapsed = sharedAI.timeElapsed;

		this.CustomInit(this.gameState);
	}

	/** AIs override this function */
	CustomInit()
	{
	}

	HandleMessage(state, playerID, sharedAI)
	{
		PlayerID = playerID;
		this.territoryMap = sharedAI.territoryMap;
		this.OnUpdate(sharedAI);
	}

	/** AIs override this function */
	OnUpdate()
	{
	}

	chat(message)
	{
		Engine.PostCommand(PlayerID, { "type": "aichat", "message": message });
	}
}
