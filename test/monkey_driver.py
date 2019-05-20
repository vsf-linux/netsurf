#!/usr/bin/python3

import sys, getopt, yaml, time

from monkeyfarmer import Browser

class DriverBrowser(Browser):
    def __init__(self, *args, **kwargs):
        super(DriverBrowser, self).__init__(*args, **kwargs)
        self.auth = []

    def add_auth(self, url, realm, username, password):
        self.auth.append((url, realm, username, password))

    def remove_auth(self, url, realm, username, password):
        keep = []
        def matches(a,b):
            if a is None or b is None:
                return True
            return a == b
        for (iurl, irealm, iusername, ipassword) in self.auth:
            if not (matches(url, iurl) or
                    matches(realm, irealm) or
                    matches(username, iusername) or
                    matches(password, ipassword)):
                keep.append((iurl, irealm, iusername, ipassword))
        self.auth = keep

    def handle_ready_login(self, logwin):
        # We have logwin.{url,username,password,realm}
        # We must logwin.send_{username,password}(xxx)
        # We may logwin.go()
        # We may logwin.destroy()
        def matches(a,b):
            if a is None or b is None:
                return True
            return a == b
        candidates = []
        for (url, realm, username, password) in self.auth:
            score = 0
            if matches(url, logwin.url):
                score += 1
            if matches(realm, logwin.realm):
                score += 1
            if matches(username, logwin.username):
                score += 1
            if score > 0:
                candidates.append((score, username, password))
        if candidates:
            candidates.sort()
            (score, username, password) = candidates[-1]
            print("401: Found candidate {}/{} with score {}".format(username, password, score))
            logwin.send_username(username)
            logwin.send_password(password)
            logwin.go()
        else:
            print("401: No candidate found, cancelling login box")
            logwin.destroy()

def print_usage():
    print('Usage:')
    print('  ' + sys.argv[0] + ' -m <path to monkey> -t <path to test>')

def parse_argv(argv):
    path_monkey = ''
    path_test = ''
    try:
        opts, args = getopt.getopt(argv,"hm:t:",["monkey=","test="])
    except getopt.GetoptError:
        print_usage()
        sys.exit(2)
    for opt, arg in opts:
        if opt == '-h':
            print_usage()
            sys.exit()
        elif opt in ("-m", "--monkey"):
            path_monkey = arg
        elif opt in ("-t", "--test"):
            path_test = arg

    if path_monkey == '':
        print_usage()
        sys.exit()
    if path_test == '':
        print_usage()
        sys.exit()

    return path_monkey, path_test

def load_test_plan(path):
    plan = []
    with open(path, 'r') as stream:
        try:
            plan = (yaml.load(stream))
        except:
            print (exc)
    return plan

def get_indent(ctx):
    return '  ' * ctx["depth"];

def print_test_plan_info(ctx, plan):
    print('Running test: [' + plan["group"] + '] ' + plan["title"])

def assert_browser(ctx):
    assert(ctx['browser'].started)
    assert(not ctx['browser'].stopped)

def conds_met(ctx, conds):
    # for each condition listed determine if they have been met
    #  efectively this is condition1 | condition2
    for cond in conds:
        if 'timer' in cond.keys():
            timer = cond['timer']
            elapsed = cond['elapsed']
            assert_browser(ctx)
            assert(ctx['timers'].get(timer) is not None)
            taken = time.time() - ctx['timers'][timer]["start"]
            if (taken >= elapsed):
                return True
        elif 'window' in cond.keys():
            status = cond['status']
            window = cond['window']
            assert(status == "complete") # TODO: Add more status support?
            if window == "*all*":
                # all windows must be not throbbing
                throbbing = False
                for win in ctx['windows'].items():
                    if win[1].throbbing:
                        throbbing = True
                if not throbbing:
                    return True
            else:
                win = ctx['windows'][window]
                if win.throbbing is False:
                    return True
        else:
            raise AssertionError("Unknown condition: {}".format(repr(cond)))

    return False

def run_test_step_action_launch(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert(ctx.get('browser') is None)
    assert(ctx.get('windows') is None)
    ctx['browser'] = DriverBrowser(monkey_cmd=[ctx["monkey"]], quiet=True)
    assert_browser(ctx)
    ctx['windows'] = dict()
    for arg in step.get('args', []):
        print(get_indent(ctx) + "        " + arg)
        ctx['browser'].pass_options(arg)


def run_test_step_action_window_new(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    tag = step['tag']
    assert_browser(ctx)
    assert(ctx['windows'].get(tag) is None)
    ctx['windows'][tag] = ctx['browser'].new_window(url=step.get('url'))

def run_test_step_action_window_close(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    tag = step['window']
    assert(ctx['windows'].get(tag) is not None)
    win = ctx['windows'].pop(tag)
    win.kill()
    win.wait_until_dead()
    assert(win.alive == False)

def run_test_step_action_navigate(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    assert(step.get('url') is not None)
    tag = step['window']
    print(get_indent(ctx) + "        " + tag + " --> " + step['url'])
    win = ctx['windows'].get(tag)
    assert(win is not None)
    win.go(step['url'])

def run_test_step_action_sleep_ms(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    conds = step['conditions']
    sleep_time = step['time']
    sleep = 0
    have_repeat = False
    if isinstance(sleep_time, str):
        assert(ctx['repeats'].get(sleep_time) is not None)
        repeat = ctx['repeats'].get(sleep_time)
        sleep = repeat["i"] / 1000
        start = repeat["start"]
        have_repeat = True
    else:
        sleep = time / 1000
        start = time.time()

    while True:
        slept = time.time() - start
        if conds_met(ctx, conds):
            if have_repeat:
                ctx['repeats'][sleep_time]["loop"] = False
            print(get_indent(ctx) + "        Condition met after {}s".format(slept))
            break
        elif slept > sleep:
            print(get_indent(ctx) + "        Condition not met after {}s".format(sleep))
            break
        else:
            ctx['browser'].farmer.loop(once=True)

def run_test_step_action_block(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    conds = step['conditions']
    assert_browser(ctx)
    
    while not conds_met(ctx, conds):
        ctx['browser'].farmer.loop(once=True)

def run_test_step_action_repeat(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    tag = step['tag']
    assert(ctx['repeats'].get(tag) is None)
    ctx['repeats'][tag] = {
        "i": step["min"],
        "step": step["step"],
        "loop": True,
    }
    while ctx['repeats'][tag]["loop"]:
        ctx['repeats'][tag]["start"] = time.time()
        ctx["depth"] += 1
        for s in step["steps"]:
            run_test_step(ctx, s)
        ctx['repeats'][tag]["i"] += ctx['repeats'][tag]["step"]
        ctx["depth"] -= 1

def run_test_step_action_plot_check(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    win = ctx['windows'][step['window']]
    checks = step['checks']
    all_text = []
    bitmaps = []
    for plot in win.redraw():
        if plot[0] == 'TEXT':
            all_text.extend(plot[6:])
        if plot[0] == 'BITMAP':
            bitmaps.append(plot[1:])
    all_text = " ".join(all_text)
    if checks is not None:
        for check in checks:
            if 'text-contains' in check.keys():
                print("Check {} in {}".format(repr(check['text-contains']),repr(all_text)))
                assert(check['text-contains'] in all_text)
            elif 'text-not-contains' in check.keys():
                print("Check {} NOT in {}".format(repr(check['text-not-contains']),repr(all_text)))
                assert(check['text-not-contains'] not in all_text)
            elif 'bitmap-count' in check.keys():
                print("Check bitmap count is {}".format(int(check['bitmap-count'])))
                assert(len(bitmaps) == int(check['bitmap-count']))
            else:
                raise AssertionError("Unknown check: {}".format(repr(check)))

def run_test_step_action_timer_start(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    tag = step['tag']
    assert_browser(ctx)
    assert(ctx['timers'].get(tag) is None)
    ctx['timers'][tag] = {}
    ctx['timers'][tag]["start"] = time.time()

def run_test_step_action_timer_restart(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    timer = step['timer']
    assert_browser(ctx)
    assert(ctx['timers'].get(timer) is not None)
    taken = time.time() - ctx['timers'][timer]["start"]
    print(get_indent(ctx) + "        {} restarted at: {:.2f}s".format(timer, taken))
    ctx['timers'][timer]["taken"] = taken
    ctx['timers'][timer]["start"] = time.time()

def run_test_step_action_timer_stop(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    timer = step['timer']
    assert_browser(ctx)
    assert(ctx['timers'].get(timer) is not None)
    taken = time.time() - ctx['timers'][timer]["start"]
    print(get_indent(ctx) + "        " + timer + " took: " + str(taken) + "s")
    ctx['timers'][timer]["taken"] = taken

def run_test_step_action_timer_check(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    condition = step["condition"].split()
    assert(len(condition) == 3)
    timer1 = ctx['timers'].get(condition[0])
    timer2 = ctx['timers'].get(condition[2])
    assert(timer1 is not None)
    assert(timer2 is not None)
    assert(timer1["taken"] is not None)
    assert(timer2["taken"] is not None)
    assert(condition[1] in ('<', '>'))
    if condition[1] == '<':
        assert(timer1["taken"] < timer2["taken"])
    elif condition[1] == '>':
        assert(timer1["taken"] > timer2["taken"])

def run_test_step_action_add_auth(ctx, step):
    print(get_indent(ctx) + "Action:" + step["action"])
    assert_browser(ctx)
    browser = ctx['browser']
    browser.add_auth(step.get("url"), step.get("realm"),
                     step.get("username"), step.get("password"))


def run_test_step_action_remove_auth(ctx, step):
    print(get_indent(ctx) + "Action:" + step["action"])
    assert_browser(ctx)
    browser = ctx['browser']
    browser.remove_auth(step.get("url"), step.get("realm"),
                        step.get("username"), step.get("password"))


def run_test_step_action_clear_log(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    browser = ctx['browser']
    tag = step['window']
    print(get_indent(ctx) + "        " + tag + " Log cleared")
    win = ctx['windows'].get(tag)
    assert(win is not None)
    win.clear_log()


def run_test_step_action_wait_log(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    browser = ctx['browser']
    tag = step['window']
    source = step.get('source')
    foldable = step.get('foldable')
    level = step.get('level')
    substr = step.get('substring')
    print(get_indent(ctx) + "        " + tag + " Wait for logging")
    win = ctx['windows'].get(tag)
    assert(win is not None)
    win.wait_for_log(source=source, foldable=foldable, level=level, substr=substr)


def run_test_step_action_js_exec(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    browser = ctx['browser']
    tag = step['window']
    cmd = step['cmd']
    print(get_indent(ctx) + "        " + tag + " Run " + cmd)
    win = ctx['windows'].get(tag)
    assert(win is not None)
    win.js_exec(cmd)


def run_test_step_action_quit(ctx, step):
    print(get_indent(ctx) + "Action: " + step["action"])
    assert_browser(ctx)
    browser = ctx.pop('browser')
    windows = ctx.pop('windows')
    assert(browser.quit_and_wait())

step_handlers = {
    "launch":       run_test_step_action_launch,
    "window-new":   run_test_step_action_window_new,
    "window-close": run_test_step_action_window_close,
    "navigate":     run_test_step_action_navigate,
    "sleep-ms":     run_test_step_action_sleep_ms,
    "block":        run_test_step_action_block,
    "repeat":       run_test_step_action_repeat,
    "timer-start":  run_test_step_action_timer_start,
    "timer-restart":run_test_step_action_timer_restart,
    "timer-stop":   run_test_step_action_timer_stop,
    "timer-check":  run_test_step_action_timer_check,
    "plot-check":   run_test_step_action_plot_check,
    "add-auth":     run_test_step_action_add_auth,
    "remove-auth":  run_test_step_action_remove_auth,
    "clear-log":    run_test_step_action_clear_log,
    "wait-log":     run_test_step_action_wait_log,
    "js-exec":      run_test_step_action_js_exec,
    "quit":         run_test_step_action_quit,
}

def run_test_step(ctx, step):
    step_handlers[step["action"]](ctx, step)

def walk_test_plan(ctx, plan):
    ctx["depth"] = 0
    ctx["timers"] = dict()
    ctx['repeats'] = dict()
    for step in plan["steps"]:
        run_test_step(ctx, step)

def run_test_plan(ctx, plan):
    print_test_plan_info(ctx, plan)
    walk_test_plan(ctx, plan)

def run_preloaded_test(path_monkey, plan):
    ctx = {
        "monkey": path_monkey,
    }
    run_test_plan(ctx, plan)

def main(argv):
    ctx = {}
    path_monkey, path_test = parse_argv(argv)
    plan = load_test_plan(path_test)
    ctx["monkey"] = path_monkey
    run_test_plan(ctx, plan)

# Some python weirdness to get to main().
if __name__ == "__main__":
    main(sys.argv[1:])
