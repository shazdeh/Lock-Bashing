import skse;
import Shared.GlobalFunc;
import skyui.util.GlobalFunctions;

class LockBashing extends MovieClip {

    public static var instance;

    public var HudMenu:MovieClip;
    public var Crosshair:MovieClip;

    private var config:Array;

    function LockBashing() {
        LockBashing.instance = this;
    }

    function onLoad() {
        HudMenu = _parent._parent;
        Crosshair = HudMenu.Crosshair;
        config = _parent._name.split('_');
        adjustPosition();
    }

    function adjustPosition() {
        _xscale = _yscale = parseInt(config[1]);
        _x = Crosshair._x + parseInt(config[2]);
        _y = Crosshair._y + parseInt(config[3]);
    }

    // @api
    function setState(a_state:Number) {
        gotoAndStop(a_state + 1);
    }
}