/**
 * This class manages the population counter in the top panel.
 * It flashes the counter if the training of any owned entity is blocked.
 */
class CounterPopulation
{
	constructor(resCode, panel, icon, count, stats)
	{
		this.resCode = resCode;
		this.panel = panel;
		this.icon = icon;
		this.count = count;
		this.count.onTick = this.onTick.bind(this);
		this.isTrainingBlocked = false;
		this.color = this.DefaultPopulationColor;
		this.stats = stats;
	}

	rebuild(playerState, getAllyStatTooltip)
	{
		const state = Object.fromEntries(Object.entries(playerState).map(([key, value]) =>
			[key, value === Infinity ? translateWithContext("In other places refered as 'Unlimited', here is to litle space.", "∞") : value]));
		this.count.caption = sprintf(translate(this.CounterCaption), state);
		let total = 0;
		for (const resCode of g_ResourceData.GetCodes())
			total += playerState.resourceGatherers[resCode];

		const colorizedTotal = coloredText(total,
			total ? this.DefaultTotalGatherersColor : this.DefaultTotalGatherersColorZero);
		this.stats.caption = colorizedTotal;

		this.isTrainingBlocked = playerState.trainingBlocked;

		this.panel.tooltip =
			setStringTags(translate(this.PopulationTooltip), CounterManager.ResourceTitleTags) +
			getAllyStatTooltip(this.getTooltipData.bind(this)) + "\n" +
			sprintf(this.CurrentGatherersTooltip, { "currentGatherers": colorizedTotal });
	}

	getTooltipData(playerState, playername)
	{
		return {
			"playername": playername,
			"statValue": sprintf(translate(this.AllyPopulationTooltip), playerState),
			"orderValue": playerState.popCount
		};
	}

	onTick()
	{
		if (this.panel.hidden)
			return;

		const newColor = this.isTrainingBlocked && Date.now() % 1000 < 500 ?
			this.PopulationAlertColor :
			this.DefaultPopulationColor;

		if (newColor == this.color)
			return;

		this.color = newColor;
		this.count.textcolor = newColor;
	}
}
// Translation: Do not insert spaces around the slash symbol for this exact string. Keep only one space between popLimit and popMax.
CounterPopulation.prototype.CounterCaption = markForTranslation("%(popCount)s/%(popLimit)s (%(popMax)s)");

CounterPopulation.prototype.PopulationTooltip = markForTranslation("Population: current/limit (max)");

CounterPopulation.prototype.AllyPopulationTooltip = markForTranslation("%(popCount)s/%(popLimit)s (%(popMax)s)");

/**
 * Storing the translated and formatted gatherer string in the prototype.
 * Including the number might seem redundant but is required since the collor
 * isn't enough to associate it with the number in the top panel.
 */
CounterPopulation.prototype.CurrentGatherersTooltip =
	setStringTags(translate("Current gatherers: %(currentGatherers)s"), { "font": "sans-14" });

/**
 * Color to highlight the total number of gatherers at zero.
 */
CounterPopulation.prototype.DefaultTotalGatherersColorZero = "200 200 200";

/**
 * Color to highlight the total number of gatherers.
 */
CounterPopulation.prototype.DefaultTotalGatherersColor = "gold";

/**
 * Colors to flash when pop limit reached.
 */
CounterPopulation.prototype.DefaultPopulationColor = "white";
CounterPopulation.prototype.PopulationAlertColor = "orange";
