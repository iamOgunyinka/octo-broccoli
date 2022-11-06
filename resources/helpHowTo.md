Correlator
==========

### The idea

The term correlation, according to the [corporate finance insitute](https://corporatefinanceinstitute.com/resources/data-science/correlation/), is a statistical measure of the relationship between two variables. The measure is best used in variables that demonstrate a linear relationship between each other. The fit of the data can be visually represented in a scatterplot (or linear plot). Using a plot, we can generally assess the relationship between the variables and determine whether they are correlated or not.

### The problem

As an example, in the crypto world, if the price of BTC is $21'000, we could plot on a graph, the price changes over a period of time. The Y-axis holding the price as it rise and falls. The X-axis holding the time as it goes by. The graph may, on the Y-axis, show prices go from a minimum of $20'000 to a maximum of $22'000. So, as the price changes for BTC, the graph data is plotted between the minimum and the maximum value given. What if we add one more crypto token to the graph, say, Dogecoin? The price of Dogecoin, as at the time of writing this documentation, is $0.12. We can easily see the problem that may arise from plotting the data for Dogecoin token alongside that of BTC. The minimum price for BTC, prior adding Dogecoin to the graph, was $20'000. It would take a very very very large graph screen to be able to plot the price changes for dogecoin at $0.12 side by side with BTC at that price.

### A solution

This is where the correlation idea comes in. We need a way to truly **present** data from random variables (in our case crypto tokens) with small or large disparity such that they appear as though the data presented have a very tiny difference. The idea implemented in this program is described below.

For each token specified by the user:

*   get the first price of the token in the market
*   find the minimum and the maximum price of the token
    *   `minimumPrice` = `firstPrice` \* 0.25
    *   `maximumPrice` = `firstPrice` \* 1.25
*   for each price obtained after the first price
    *   `minimumPrice` = `std::min(minimumPrice, currentPrice)`
    *   `maximumPrice` = `std::max(maximumPrice, currentPrice)`
    *   `normalizedPrice` = `(currentPrice - minimumPrice) / (maximumPrice - minimumPrice)`
*   the `normalizedPrice` is the price that is plotted on the graph scene.

Correlator (the project)
------------------------

There are a number of widgets in the Correlator program and it may be difficult to know what each one does at first glance. This documentation is meant to explain how the program works, so let's start first with explanation on the widgets.

### The launch

At the launch of the correlator program, you are presented with the main window. This window is intentionally kept simple, it has a menu bar and a toolbar. On these bars, you are presented with the options to open `New trade`, `Reload trade`, `API Key`, `Using correlator` and `About`.

![image](https://user-images.githubusercontent.com/4146085/200158153-04b15650-72a0-4113-9ad1-ff838fc84deb.png)

### About (on the toolbar)

This window shows information about the software used in the development of the program - Qt. It goes on to explain what Qt is, what platforms it works on, the licenses etc.

Using correlator (on the toolbar)
---------------------------------

This is the window that is opened right now. It is the documentation that describes how correlator works.

### New trade

This is the most important part of the program. Pressing the `New trade` button on the main window's toolbar opens a child window that is located inside of the main window itself(See image #2).

![image](https://user-images.githubusercontent.com/4146085/200158204-83cb9c66-4c5f-4ef2-b191-8c4b34907bb2.png)

To use this window, there are three tabs we need to get familiar with, namely `Settings`, `Charts` and the `Order table`.

![image](https://user-images.githubusercontent.com/4146085/200158132-2ee7fd09-a77c-473b-9013-caeb7c0f733d.png)

### The settings tab

This is where majority of the widgets are. Click on the `Settings` tab and let's walk through it together.

*   The exchanges (see image below)
    *   This correlator implemented APIs for three exchanges
        *   Binance, FTX and Kucoin
    *   As the user select an exchange, the two boxes below them, namely Spot and Futures, changes.
    *   This is because each exchange deals with different kinds of spot and futures token and sometimes the name and convention used to specify tokens, say BTC, differs.
    *   The `Spots` box always hold the list of spot tokens that can be traded on an exchange
    *   The `Futures` box hold the list of futures token that can be traded on an exchange too.

![image](https://user-images.githubusercontent.com/4146085/200158328-7511c202-9f80-4019-9171-9c117d8151ff.png)

We mentioned above that, to use a correlator, we would need to plot data on the graph, of price changes of crypto tokens. That means, we would have to manually select spot or futures token from the list we discussed above and the tokens could be from any of the three exchanges implemented -- Binance, FTX or Kucoin.

To select a token:

*   First **uncheck** the check box that says `Activate the price delta widget below` (more explanation on this later in the price average section)
*   Select the exchange you want
*   To select any spot token
    *   Pick from the list in the SPOT box and press the button in front of the list that says `>>`
*   To select futures
    *   Pick from the list in the FUTURES box and press the button with `>>` directly in front.
*   The current selected token in the spot or futures box would be listed on the second list widget titled `Correlator plot tokens`
*   To remove a selected token from the list, click on the name of the token to be removed and click on the `<<` button.

![image](https://user-images.githubusercontent.com/4146085/200160087-7d1e3e0f-7203-4778-ac69-fb6c65adb5da.png)

### Tradable and reference tokens

The maximum number of tokens that can be traded in the correlator is **one**. However, we could use the prices of other tokens from one or more exchanges as references. Prices of reference tokens are used to determine when to buy or sell the specified token. When we select a token to be added to the list, we can specify which ONE to be traded and the others whose correlated/normalized prices should be used as references. All reference tokens are combined (by finding the `mean` of their normalized prices) and they are drawn as one data line on the graph.

*   To select the token to be traded, follow the steps above
    *   but uncheck the box (in the image below), the box captioned `Use as ref`
*   To select tokens to the used as references (aka ref)
    *   Select the `Use as ref` checkbox and follow the steps above.
*   Reference tokens end with asterisks `*`
*   Tradable token do not.

![image](https://user-images.githubusercontent.com/4146085/200160998-054011b8-026d-4d1e-a27c-dc1d44f05487.png)

NB: Tokens that can be traded must not exceed ONE. Reference tokens can be one or more, no limit is set on reference tokens.

#### The great reset

In the section titled `A solution`, we usually find the minimum and maximum price the first time we get the price of a specific token. That's ordinarily the default, the first time. However, we could tell the program to find the minimum and maximum price after a certain number of graph ticks. There are four kinds of reset that we can specify:

*   Reset ONLY normal (the tradable token) data line
*   Reset ONLY ref (the reference tokens) data line
*   Reset BOTH normal and ref lines
*   Special reset

We can specify to the program to reset the lines after `X` ticks. The input the `X` in the input line captioned: `Reset every X ticks`. Say, for example, we input 5'000 in the `Reset every X ticks` input line WHILE selecting `Normal` in the box captioned `Type`, then for every 5000 ticks, the normal line (the data line for the tradable token) is reset by following the formula in the `A solution` section. Same principle is applied when the `Type` is specified as `ref`, it resets only the ref line. We can also always reset both the normal and the ref lines at the same time.

The **special** reset is used to close the gap between the reference line and the normal line. It doesn't use the `Reset every X ticks` input line. It works like so:

*   for every tick, it checks if the gap/disparity between the normal and the ref has exceeded a certain amount
*   if yes, it closes the gap to another user-specified distance.
*   The input line captioned `Max % difference allowed` is where the user specifies the maximum gap allowed
*   the input line captioned `Reduce lines to this %` is where the user specifies the distance it wants it to have.

For special reset, say, we specify `1` in `Max % difference allowed` (which is 1%), and `0.1` in `Reduce lines to this %` -- when the gap between the ref and normal line reaches 1%, the gap is reduced to 0.1%. Then the program continues. The gap is closed ONLY if the gap >= the value specified in `Max % difference allowed` input line.

![image](https://user-images.githubusercontent.com/4146085/200160431-f2133204-3882-4baa-95ef-d510ef1c02ce.png)

### Defining the graph to your taste.

A number of things can be tuned on the graph scene to make it presentable. For example, the legends -- which are the labels that shows the color used and names of the lines representing the normal and reference lines. The `graph thickness` can be changed from 1 to 5 to change the thickness of each lines on the graph scene. The `show data for` box is used to change the amount of data shown on the graph. The default is `100 seconds`, which means the graph scene only show data for the last 100 seconds. The `Periodic tick` box is used to specify how often the graph data is updated. Default is 100 ms and it almost always suffice to leave it as-is.

![image](https://user-images.githubusercontent.com/4146085/200161999-1fe7357a-f3dd-482b-bc45-4add737cdb8d.png)

### The threshold and trade initialization

In the normalization "world", trades are initiated when the ref graph line and the tradable/normal graph line makes a crossover AND then a certain threshold is opened. This threshold is indicated in the `Umbral` input line. Say, for example, if the ref and the normal line makes a cross-over, and then after a while the gap between the two lines reaches the amount specified in the `Umbral` line, a trade is initiated.

What SIDE is opened is dependent on what side of the line is above and what side is below.

If the `Reverse` checkbox is unchecked:

*   the red being on top and the green line being at the bottom initiates a `SELL` trade
*   the green line being on top and the red being at the bottom initiates a `BUY` order With the `Reverse` checkbox checked, the reverse of the order is the case, that is:
*   the red being on top and the green line being at the bottom initiates a `BUY` order
*   the green line being on top and the red being at the bottom initiates a `SELL` order

For every cross-over that happens, as long as the conditions above is satisfied, a trade will always be initiated. It could be hundreds or thousands of `BUY` or `SELL` orders over and over again. That means you could be buying and buying without one single SELL or you could be selling and selling without a single BUY. Thus, to limit the buying and selling to only happen interchangeably (i.e you must sell after a buy and can only buy after a SELL), one must check the `One operation only` box. That limits the orders to BUY after SELL and SELL after BUY. This solves the problem of BUY BUY BUY BUY BUY or SELL SELL SELL SELL.

If an order fails(rarely so), perhaps because of poor network connection, the program tries over and over to initiate the same order again. The user can indicate in the `Max retries` box the maximum number of time the program is allowed to retry failed orders -- the default is 10.

![image](https://user-images.githubusercontent.com/4146085/200162181-8014b45d-21fc-4cd5-b2e5-56aa2a513af6.png)

### Test drive or not

Unchecking the box labelled "Live trade" allows the user test the program without actually doing any trade on any of the exchanges. Checking it initiates live trade directly on any of the exchanges the tradable/normal token selected is from.

![image](https://user-images.githubusercontent.com/4146085/200162769-d84914c2-c46e-4fbb-99c6-4127d5cc7618.png)

Price averaging
---------------

Before now, we have talked exclusively on price normalization and how we initiate order when a cross-over happens on the data plotted on the graph. We have another way of initiating orders and this is based on price averaging. To do price averaging, the user picks at most **two** tokens from the list of exchange tokens -- one spot and one futures token. The price averaging works by:

`dataToPlotOnGraph = (priceOfA + priceOfB) / priceOfB;`

To select tokens to be used for price averaging, the specific widget box (shown below) is used, and the user must select the `Activate the price delta widget below` checkbox(also shown below). Only then would the program know that the selected tokens is meant for price averaging.

![image](https://user-images.githubusercontent.com/4146085/200163623-c7d3c613-bb68-4a3e-858f-e2c064f1220e.png)

Now that we have two means of generating orders, normalization (which we discussed extensively above) and price averaging (which is being discussed now), we can specify to the program which of it we would like to use:

*   Allow normal (normalization orders) + average (price averaging orders) trade
*   ONLY normal orders
*   ONLY price average orders.

The `Find average every X seconds` is used to instruct the program to calculate the price average after every `X` seconds. That is, 10 means _calculate the price average every 10 seconds_.

The `Make order if price >= AVG + N` field is used to tell the program that an order should be initiated when the current data being plotted at a specific instance is greater than or equals to user-input number `+` the last calculated price average.

When the box `Use last saved average at startup` is checked, the program saves the average used when the program was last run and then one subsequent runs, that average is the first average used.

The `Charts` tab
----------------

This is the tab that holds the normalization (top graph) and price averaging (bottom) graphs.

![image](https://user-images.githubusercontent.com/4146085/200163396-1e91c3c3-53c5-4f87-838e-ee98533952cf.png)

The `Order table` tab
---------------------

This tab shows the list of all generated orders and the orders generated can be exported as a CSV file.

![image](https://user-images.githubusercontent.com/4146085/200163377-be831139-9754-468c-b8a7-e41d9205f11a.png)
