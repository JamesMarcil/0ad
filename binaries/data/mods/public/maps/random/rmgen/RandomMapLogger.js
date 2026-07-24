function RandomMapLogger()
{
	this.taskStarted = false;
	this.startTime = Engine.GetMicroseconds ? Engine.GetMicroseconds() : 0;
	this.prefix = ""; // seems noisy

	// Don't print in test cases
	if (g_MapSettings.Name)
		this.printDirectly(
			this.prefix +
			"Generating " + g_MapSettings.Name +
			" of size " + g_MapSettings.Size +
			" and " + (g_MapSettings.PlayerData.length - 1) + " players.\n");
}

RandomMapLogger.prototype.printDirectly = function(string)
{
	log(string);
	print(string);
};

RandomMapLogger.prototype.print = function(string)
{
	if (this.taskStarted)
		Engine.ProfileStop();

	const text = this.prefix + string;
	this.printDirectly(text + "...\n");
	Engine.ProfileStart(text);
	this.taskStarted = true;
};

RandomMapLogger.prototype.close = function()
{
	if (this.taskStarted)
		Engine.ProfileStop();
	this.printDirectly(this.prefix + "Map generation finished.\n");
};
