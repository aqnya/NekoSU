package me.nekosu.aqnya.ui.screens

import android.app.Application
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.delay
import me.nekosu.aqnya.core.navigation.AppRoute
import me.nekosu.aqnya.data.prefs.AppPreferences
import me.nekosu.aqnya.feature.home.presentation.HomeViewModel
import me.nekosu.aqnya.feature.home.presentation.HomeViewModelFactory
import me.nekosu.aqnya.feature.home.ui.HomeScreen
import me.nekosu.aqnya.ui.animation.AnimatedBottomNavBar
import me.nekosu.aqnya.ui.animation.PageTransitions
import me.nekosu.aqnya.ui.component.NonProductionRibbon
import me.nekosu.aqnya.ui.navigation.BottomNavItem
import me.nekosu.aqnya.ui.navigation.NormalBottomNavigationBar
import me.nekosu.aqnya.ui.permission.AppPermission
import me.nekosu.aqnya.ui.permission.MiuiPermissionUtils
import me.nekosu.aqnya.ui.permission.rememberPermissionState
import me.nekosu.aqnya.ui.screens.install.InstallScreen
import me.nekosu.aqnya.ui.screens.modules.ModuleScreen
import me.nekosu.aqnya.ui.screens.settings.SettingsScreen

// 根据 Tab 在 navItems 中的相对位置决定滑入/滑出方向，
// 非 Tab 路由（about/settings 详情等）一律走 fade，交给 PageTransitions 处理。
private fun tabTransitionEnter(
    navItems: List<BottomNavItem>,
    motionScheme: MotionScheme,
): AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition =
    {
        val routes = navItems.map { it.route }
        val initialRoute = initialState.destination.route
        val targetRoute = targetState.destination.route

        if (initialRoute in routes && targetRoute in routes) {
            val initialIndex = navItems.indexOfFirst { it.route == initialRoute }
            val targetIndex = navItems.indexOfFirst { it.route == targetRoute }
            if (targetIndex > initialIndex) {
                slideInHorizontally(
                    animationSpec = motionScheme.defaultSpatialSpec(),
                    initialOffsetX = { it / 4 },
                ) + fadeIn(animationSpec = motionScheme.defaultEffectsSpec())
            } else {
                slideInHorizontally(
                    animationSpec = motionScheme.defaultSpatialSpec(),
                    initialOffsetX = { -it / 4 },
                ) + fadeIn(animationSpec = motionScheme.defaultEffectsSpec())
            }
        } else {
            fadeIn(animationSpec = motionScheme.defaultEffectsSpec())
        }
    }

private fun tabTransitionExit(
    navItems: List<BottomNavItem>,
    motionScheme: MotionScheme,
): AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition =
    {
        val routes = navItems.map { it.route }
        val initialRoute = initialState.destination.route
        val targetRoute = targetState.destination.route

        if (initialRoute in routes && targetRoute in routes) {
            val initialIndex = navItems.indexOfFirst { it.route == initialRoute }
            val targetIndex = navItems.indexOfFirst { it.route == targetRoute }
            if (targetIndex > initialIndex) {
                slideOutHorizontally(
                    animationSpec = motionScheme.fastSpatialSpec(),
                    targetOffsetX = { -it / 4 },
                ) + fadeOut(animationSpec = motionScheme.fastEffectsSpec())
            } else {
                slideOutHorizontally(
                    animationSpec = motionScheme.fastSpatialSpec(),
                    targetOffsetX = { it / 4 },
                ) + fadeOut(animationSpec = motionScheme.fastEffectsSpec())
            }
        } else {
            fadeOut(animationSpec = motionScheme.fastEffectsSpec())
        }
    }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val navController = rememberNavController()
    val context = LocalContext.current
    val motionScheme = MaterialTheme.motionScheme

    val homeViewModel: HomeViewModel =
        viewModel(
            factory = HomeViewModelFactory(context.applicationContext as Application),
        )
    val showRules by AppPreferences.showRulesFlow(context).collectAsState(initial = false)
    val navItems = remember(showRules) { BottomNavItem.items(showRules) }

    val miuiAppsPermState = rememberPermissionState(AppPermission.MIUI_GET_INSTALLED_APPS)

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route
    val bottomBarRoutes = remember(navItems) { navItems.map { it.route }.toSet() }
    val showBottomBar = currentRoute in bottomBarRoutes
    val selectedIndex = navItems.indexOfFirst { it.route == currentRoute }.coerceAtLeast(0)

    LaunchedEffect(currentRoute) {
        if (currentRoute == BottomNavItem.Home.route) {
            delay(300)
            homeViewModel.refresh()
        }
    }

    LaunchedEffect(Unit) {
        if (MiuiPermissionUtils.isSupportedOnThisDevice(context) &&
            !MiuiPermissionUtils.isGranted(context)
        ) {
            miuiAppsPermState.launchRequest()
        }
    }

    val onTabClick: (Int) -> Unit = { index ->
        val target = navItems.getOrNull(index)?.route
        if (target != null && target != currentRoute) {
            navController.navigate(target) {
                // 回到 Tab 根，保存/恢复各 Tab 的滚动状态等
                popUpTo(BottomNavItem.Home.route) {
                    saveState = true
                }
                launchSingleTop = true
                restoreState = true
            }
        }
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        contentWindowInsets =
            WindowInsets.safeDrawing.only(
                WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal,
            ),
        bottomBar = {
            AnimatedBottomNavBar(
                visible = showBottomBar,
                items = navItems,
                selectedIndex = selectedIndex,
                onTabClick = onTabClick,
            ) { items, selIndex, onClick ->
                NormalBottomNavigationBar(
                    items = items,
                    selectedIndex = selIndex,
                    onTabClick = onClick,
                )
            }
        },
    ) { innerPadding ->
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding),
        ) {
            NavHost(
                navController = navController,
                startDestination = BottomNavItem.Home.route,
                modifier = Modifier.fillMaxSize(),
            ) {
                composable(
                    route = BottomNavItem.Home.route,
                    enterTransition = tabTransitionEnter(navItems, motionScheme),
                    exitTransition = tabTransitionExit(navItems, motionScheme),
                ) {
                    HomeScreen(
                        viewModel = homeViewModel,
                        onInstallClick = { navController.navigate(AppRoute.Install) },
                    )
                }

                composable(
                    route = BottomNavItem.History.route,
                    enterTransition = tabTransitionEnter(navItems, motionScheme),
                    exitTransition = tabTransitionExit(navItems, motionScheme),
                ) {
                    AppListScreen(
                        navController = navController,
                        extraBottomPadding = 12.dp,
                    )
                }

                composable(
                    route = BottomNavItem.Modules.route,
                    enterTransition = tabTransitionEnter(navItems, motionScheme),
                    exitTransition = tabTransitionExit(navItems, motionScheme),
                ) {
                    ModuleScreen()
                }

                composable(
                    route = BottomNavItem.Settings.route,
                    enterTransition = tabTransitionEnter(navItems, motionScheme),
                    exitTransition = tabTransitionExit(navItems, motionScheme),
                ) {
                    SettingsScreen(navController)
                }

                composable(
                    route = AppRoute.About,
                    enterTransition = { PageTransitions.defaultEnter(motionScheme) },
                    exitTransition = { PageTransitions.defaultExit(motionScheme) },
                ) {
                    AboutScreen(navController)
                }

                composable(
                    route = AppRoute.OpenSource,
                    enterTransition = { PageTransitions.defaultEnter(motionScheme) },
                    exitTransition = { PageTransitions.defaultExit(motionScheme) },
                ) {
                    OpenSourceScreen(navController)
                }

                composable(
                    route = AppRoute.Install,
                    enterTransition = { PageTransitions.defaultEnter(motionScheme) },
                    exitTransition = { PageTransitions.defaultExit(motionScheme) },
                ) {
                    InstallScreen(navController)
                }

                composable(AppRoute.AppDetailPattern) { backStackEntry ->
                    val pkg = backStackEntry.arguments?.getString("packageName")
                    if (pkg == null) {
                        LaunchedEffect(Unit) { navController.popBackStack() }
                        return@composable
                    }
                    val appViewModel: AppViewModel = viewModel(factory = AppViewModelFactory(context.applicationContext))
                    val app = appViewModel.allApps.find { it.packageName == pkg }

                    if (app != null) {
                        AppDetailScreen(
                            app = app,
                            config = appViewModel.appConfigs[pkg],
                            onSave = { appViewModel.setAppConfig(app, it) },
                            onBack = { navController.popBackStack() },
                        )
                    } else {
                        LaunchedEffect(Unit) {
                            appViewModel.loadApps()
                            if (appViewModel.allApps.none { it.packageName == pkg }) {
                                navController.popBackStack()
                            }
                        }
                        Box(
                            modifier = Modifier.fillMaxSize(),
                            contentAlignment = Alignment.Center,
                        ) {
                            LoadingState()
                        }
                    }
                }
            }

            NonProductionRibbon(modifier = Modifier.align(Alignment.TopEnd))
        }
    }
}

@Preview(showBackground = true)
@Composable
fun MainScreenPreview() {
    MainScreen()
}
