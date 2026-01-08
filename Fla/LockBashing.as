import skse;
import Shared.GlobalFunc;
import skyui.util.GlobalFunctions;

class LockBashing extends MovieClip {

    public static var instance;

    public var HudMenu:MovieClip;
    public var hudAnchor:MovieClip;

    private var scale:Number;
    private var xOffset:Number;
    private var yOffset:Number;

    function LockBashing() {
        LockBashing.instance = this;
    }

    function onLoad() {
        HudMenu = _parent._parent;
        hudAnchor = HudMenu.Crosshair;
        onEnterFrame = function() {
            skse.SendModEvent('Lockbashing_WidgetLoaded');
            onEnterFrame = null;
        };
    }

    function adjustPosition() {
        _xscale = _yscale = scale;
        _x = hudAnchor._x + xOffset;
        _y = hudAnchor._y + yOffset;
    }

    // @api
    function setState(a_state:Number) {
        gotoAndStop(a_state + 1);
    }

    function setOptions(a_scale:Number, a_xOffset:Number, a_yOffset:Number) {
        scale = a_scale;
        xOffset = a_xOffset;
        yOffset = a_yOffset;
        adjustPosition();
    }
}